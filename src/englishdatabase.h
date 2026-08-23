/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * English word database ported from ibus-libpinyin's EnglishDatabase
 * (src/PYEnglishDatabase.{h,cc}).  Schema, SQL text, and the
 * in-memory-userdb-with-debounced-file-backup design are identical to
 * upstream; glib plumbing (GTimer/g_timeout, g_file utilities, GString
 * printf) is replaced by the fcitx event loop and the C++ standard
 * library.
 */
#ifndef FCITX5_OXPINYIN_ENGLISHDATABASE_H_
#define FCITX5_OXPINYIN_ENGLISHDATABASE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>

#include <sqlite3.h>

namespace fcitx {

class EnglishDatabase {
public:
    explicit EnglishDatabase(EventLoop *eventLoop);
    ~EnglishDatabase();

    // Opens the read-only system word list and attaches an in-memory copy
    // of the user database (created on first use); false leaves the
    // object unusable and English mode disabled.
    bool open(const std::string &systemDb, const std::string &userDb);

    // Words matching the prefix, ordered by summed system+user frequency
    // (upstream's GLOB query, byte-identical SQL).
    bool listWords(const char *prefix, std::vector<std::string> &words);

    // Add delta to the word's user frequency, inserting it on first use.
    bool train(const char *word, float delta);

private:
    bool isDatabaseExisted(const char *filename);
    bool createDatabase(const char *filename);
    bool getUserWordInfo(const char *word, float &freq);
    bool insertUserWord(const char *word, float freq);
    bool updateUserWord(const char *word, float freq);
    bool executeSQL(sqlite3 *sqlite);
    bool loadUserDB();
    bool saveUserDB();
    void modified();

    sqlite3 *sqlite_ = nullptr;
    std::string sql_;
    std::string userDb_;

    // Upstream debounce: the user db file is rewritten once 60 s have
    // passed since the last modification, and on destruction if a save is
    // still pending.
    EventLoop *eventLoop_;
    std::unique_ptr<EventSourceTime> saveEvent_;
    uint64_t lastModified_ = 0;
    bool savePending_ = false;
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_ENGLISHDATABASE_H_
