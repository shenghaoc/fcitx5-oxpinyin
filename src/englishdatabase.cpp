/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from ibus-libpinyin src/PYEnglishDatabase.cc.  Every SQL string
 * is byte-identical to upstream, except where values are raw typed text
 * (listWords' prefix and the user-word lookup/write statements): those
 * bind parameters instead of upstream's printf-style "%s"/"%f"
 * interpolation.  Differences are documented inline.
 */
#include "englishdatabase.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <sys/stat.h>
#include <sys/types.h>

#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/log.h>

namespace fcitx {

namespace {

// DB_BACKUP_TIMEOUT (seconds -> microseconds for the fcitx event loop).
constexpr uint64_t kBackupTimeout = 60ULL * 1000000ULL;

} // namespace

EnglishDatabase::EnglishDatabase(EventLoop *eventLoop)
    : eventLoop_(eventLoop) {}

EnglishDatabase::~EnglishDatabase() {
    // Upstream destructor: flush a pending backup before closing.
    if (savePending_) {
        saveUserDB();
    }
    saveEvent_.reset();
    if (sqlite_) {
        sqlite3_close(sqlite_);
        sqlite_ = nullptr;
    }
}

bool EnglishDatabase::isDatabaseExisted(const char *filename) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(filename, ec)) {
        return false;
    }

    sqlite3 *tmpDb = nullptr;
    if (sqlite3_open_v2(filename, &tmpDb, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        sqlite3_close(tmpDb);
        return false;
    }

    // Check the desc table: the version must be upstream's '1.2.0'.
    // Upstream leaks the statement and handle on the early-error paths;
    // finalize/close on every path instead (same verdicts, ASan-clean).
    sqlite3_stmt *stmt = nullptr;
    sql_ = "SELECT value FROM desc WHERE name = 'version';";
    bool valid = false;
    if (sqlite3_prepare_v2(tmpDb, sql_.c_str(), -1, &stmt, nullptr) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW &&
            sqlite3_column_type(stmt, 0) == SQLITE_TEXT) {
            const char *version =
                reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            valid = version && std::strcmp("1.2.0", version) == 0;
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(tmpDb);
    return valid;
}

bool EnglishDatabase::createDatabase(const char *filename) {
    // Unlink the old database.
    std::error_code ec;
    if (std::filesystem::is_regular_file(filename, ec) &&
        !std::filesystem::remove(filename, ec)) {
        return false;
    }

    const auto dirname = std::filesystem::path(filename).parent_path();
    if (!dirname.empty()) {
        // g_mkdir_with_parents (dirname, 0700): std::filesystem's defaults
        // are umask-dependent, so create every missing component explicitly
        // private to the user.
        std::filesystem::path created;
        for (const auto &part : dirname) {
            created /= part;
            if (::mkdir(created.c_str(), 0700) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }

    sqlite3 *tmpDb = nullptr;
    if (sqlite3_open_v2(filename, &tmpDb,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(tmpDb);
        return false;
    }

    /* Create DESCription table */
    sql_ = "BEGIN TRANSACTION;\n"
           "CREATE TABLE IF NOT EXISTS desc (name TEXT PRIMARY KEY, value "
           "TEXT);\n"
           "INSERT OR IGNORE INTO desc VALUES ('version', '1.2.0');"
           "COMMIT;\n";
    if (!executeSQL(tmpDb)) {
        sqlite3_close(tmpDb);
        return false;
    }

    /* Create Schema */
    sql_ = "CREATE TABLE IF NOT EXISTS english ("
           "word TEXT NOT NULL PRIMARY KEY,"
           "freq FLOAT NOT NULL DEFAULT(0)"
           ");";
    if (!executeSQL(tmpDb)) {
        sqlite3_close(tmpDb);
        return false;
    }
    sqlite3_close(tmpDb);
    return true;
}

bool EnglishDatabase::open(const std::string &systemDb,
                           const std::string &userDb) {
    if (!isDatabaseExisted(systemDb.c_str())) {
        return false;
    }
    // Upstream conflates "missing" and "present but invalid" and unlinks
    // either way.  Documented divergence: only an absent database is
    // created fresh; one that exists but fails validation (unreadable,
    // corrupt, version mismatch) is renamed to "<userdb>.bak" first, so
    // learned words remain recoverable.
    std::error_code ec;
    if (std::filesystem::is_regular_file(userDb, ec)) {
        if (!isDatabaseExisted(userDb.c_str())) {
            std::filesystem::rename(userDb, userDb + ".bak", ec);
            if (ec || !createDatabase(userDb.c_str())) {
                return false;
            }
        }
    } else if (!createDatabase(userDb.c_str())) {
        return false;
    }
    /* cache the user db name. */
    userDb_ = userDb;

    /* do database attach here. :) */
    // Divergence from upstream: CREATE is dropped because the system word
    // list is a shared installed resource already verified above — never
    // fabricate a file at that path.  Upstream's flags are otherwise kept:
    // SQLITE_OPEN_READONLY is NOT usable here because SQLite makes every
    // attached schema unwritable on a READONLY connection (both the
    // backup API and plain SQL fail with SQLITE_READONLY), and this class
    // attaches the in-memory userdb to this very connection.
    if (sqlite3_open_v2(systemDb.c_str(), &sqlite_, SQLITE_OPEN_READWRITE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(sqlite_);
        sqlite_ = nullptr;
        return false;
    }
    if (!loadUserDB()) {
        sqlite3_close(sqlite_);
        sqlite_ = nullptr;
        return false;
    }
    return true;
}

/* List the words in freq order. */
bool EnglishDatabase::listWords(const char *prefix,
                                std::vector<std::string> &words) {
    sqlite3_stmt *stmt = nullptr;
    words.clear();

    /* list words */
    // Documented divergence: the prefix is raw typed text, so instead of
    // upstream's printf interpolation it is bound as a parameter, with
    // GLOB metacharacters bracket-escaped to match literally; the
    // trailing "*" keeps the prefix-match semantics.
    sql_ = "SELECT word FROM ( "
           "SELECT * FROM english UNION ALL SELECT * FROM userdb.english) "
           " WHERE word GLOB ? GROUP BY word ORDER BY SUM(freq) DESC;";
    std::string pattern;
    pattern.reserve(std::strlen(prefix) * 4 + 1);
    for (const char *p = prefix; *p; ++p) {
        switch (*p) {
        case '*':
            pattern += "[*]";
            break;
        case '?':
            pattern += "[?]";
            break;
        case '[':
            pattern += "[[]";
            break;
        case ']':
            pattern += "[]]";
            break;
        default:
            pattern += *p;
            break;
        }
    }
    pattern += '*';

    if (sqlite3_prepare_v2(sqlite_, sql_.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_text(stmt, 1, pattern.c_str(),
                          static_cast<int>(pattern.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    int result = sqlite3_step(stmt);
    while (result == SQLITE_ROW) {
        /* get the words. */
        if (sqlite3_column_type(stmt, 0) != SQLITE_TEXT) {
            sqlite3_finalize(stmt);
            return false;
        }
        words.emplace_back(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        result = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

/* Get the freq of user sqlite db: returns true when the query ran,
 * with found telling whether the word exists; false means the lookup
 * itself failed (no conclusion about the word). */
bool EnglishDatabase::getUserWordInfo(const char *word, bool &found,
                                      float &freq) {
    sqlite3_stmt *stmt = nullptr;
    found = false;
    /* get word info. */
    // Divergence from upstream's printf interpolation: word is raw typed
    // text, bound as a parameter.
    sql_ = "SELECT freq FROM userdb.english WHERE word = ?;";
    if (sqlite3_prepare_v2(sqlite_, sql_.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    if (sqlite3_bind_text(stmt, 1, word, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    bool ok = false;
    const int step = sqlite3_step(stmt);
    if (step == SQLITE_ROW && sqlite3_column_type(stmt, 0) == SQLITE_FLOAT) {
        freq = static_cast<float>(sqlite3_column_double(stmt, 0));
        found = true;
        ok = true;
    } else if (step == SQLITE_DONE) {
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

/* Update the freq with delta value. */
bool EnglishDatabase::updateUserWord(const char *word, float freq) {
    // Parameter-bound like getUserWordInfo; executeSQL() cannot bind.
    sql_ = "UPDATE userdb.english SET freq = ?1 WHERE word = ?2;";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(sqlite_, sql_.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    const bool ok =
        sqlite3_bind_double(stmt, 1, static_cast<double>(freq)) == SQLITE_OK &&
        sqlite3_bind_text(stmt, 2, word, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (ok) {
        modified();
    }
    return ok;
}

/* Insert the word into user db with the initial freq. */
bool EnglishDatabase::insertUserWord(const char *word, float freq) {
    // Parameter-bound like getUserWordInfo; executeSQL() cannot bind.
    sql_ = "INSERT INTO userdb.english (word, freq) VALUES (?1, ?2);";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(sqlite_, sql_.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return false;
    }
    const bool ok =
        sqlite3_bind_text(stmt, 1, word, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_double(stmt, 2, static_cast<double>(freq)) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (ok) {
        modified();
    }
    return ok;
}

// Divergence from upstream: a failed lookup is distinguished from a
// confirmed absence (upstream inserts on any lookup miss) and the
// write result is propagated instead of always reporting success.
bool EnglishDatabase::train(const char *word, float delta) {
    bool found = false;
    float freq = 0;
    if (!getUserWordInfo(word, found, freq)) {
        return false;
    }
    if (found) {
        freq += delta;
        return updateUserWord(word, freq);
    }
    return insertUserWord(word, delta);
}

bool EnglishDatabase::executeSQL(sqlite3 *sqlite) {
    char *errmsg = nullptr;
    if (sqlite3_exec(sqlite, sql_.c_str(), nullptr, nullptr, &errmsg) !=
        SQLITE_OK) {
        // sql_ interpolates user-typed words; log the SQLite message only.
        FCITX_WARN() << (errmsg ? errmsg : "unknown sqlite error");
        sqlite3_free(errmsg);
        return false;
    }
    sql_.clear();
    return true;
}

bool EnglishDatabase::loadUserDB() {
    sqlite3 *userdb = nullptr;
    /* Attach user database */
    do {
        sql_ = "ATTACH DATABASE ':memory:' AS userdb;";
        if (!executeSQL(sqlite_)) {
            break;
        }

        /* Note: user db is always created by open. */
        if (sqlite3_open_v2(userDb_.c_str(), &userdb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            nullptr) != SQLITE_OK) {
            break;
        }

        // Upstream ignores the backup results and carries on with an
        // empty in-memory user db when the copy fails — and the next
        // debounced save would then overwrite the user's file with that
        // empty copy.  Documented divergence: check the results so a
        // failed load fails open() closed (English mode disabled, file
        // untouched); the success path is unchanged.
        sqlite3_backup *backup =
            sqlite3_backup_init(sqlite_, "userdb", userdb, "main");
        if (!backup) {
            break;
        }
        const int step = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if (step != SQLITE_DONE || finish != SQLITE_OK) {
            break;
        }

        sqlite3_close(userdb);
        return true;
    } while (false);

    if (userdb) {
        sqlite3_close(userdb);
    }
    return false;
}

bool EnglishDatabase::saveUserDB() {
    sqlite3 *userdb = nullptr;
    const std::string tmpfile = userDb_ + "-tmp";
    do {
        /* remove tmpfile if it exist */
        std::error_code ec;
        std::filesystem::remove(tmpfile, ec);

        if (sqlite3_open_v2(tmpfile.c_str(), &userdb,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            nullptr) != SQLITE_OK) {
            break;
        }

        sqlite3_backup *backup =
            sqlite3_backup_init(userdb, "main", sqlite_, "userdb");
        if (backup == nullptr) {
            break;
        }

        // Upstream renames unconditionally; sqlite3_backup_finish rolls an
        // incomplete backup back, so the rename would replace the user's
        // data with an empty/partial file.  Documented divergence: check
        // the results and keep the previous file on failure; the success
        // path is unchanged.
        const int step = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if (step != SQLITE_DONE || finish != SQLITE_OK) {
            break;
        }
        sqlite3_close(userdb);

        std::filesystem::rename(tmpfile, userDb_, ec);
        return !ec;
    } while (false);

    if (userdb) {
        sqlite3_close(userdb);
    }
    std::error_code ec;
    std::filesystem::remove(tmpfile, ec);
    return false;
}

void EnglishDatabase::modified() {
    // Upstream restarts a GTimer and lets a 60 s periodic g_timeout save
    // once a full quiet interval has passed.  Same observable behaviour
    // with a re-armed one-shot: fire at lastModified_ + 60 s, and push
    // the deadline back if a modification (or a failed save) intervened.
    lastModified_ = now(CLOCK_MONOTONIC);
    savePending_ = true;

    if (saveEvent_) {
        saveEvent_->setTime(lastModified_ + kBackupTimeout);
        saveEvent_->setOneShot();
        return;
    }
    saveEvent_ = eventLoop_->addTimeEvent(
        CLOCK_MONOTONIC, lastModified_ + kBackupTimeout, 0,
        [this](EventSourceTime *source, uint64_t) {
            if (now(CLOCK_MONOTONIC) - lastModified_ >= kBackupTimeout &&
                saveUserDB()) {
                savePending_ = false;
                return true;
            }
            source->setTime(now(CLOCK_MONOTONIC) + kBackupTimeout);
            source->setOneShot();
            return true;
        });
}

} // namespace fcitx
