# english.db is generated from the verbatim upstream inputs (ibus-libpinyin
# data/wordlist + data/english.awk) with the exact upstream pipeline from
# its data/Makefile.am:
#   LC_NUMERIC=C awk -f english.awk wordlist | sqlite3 english.db
# (removing a partial output on failure, like upstream's `|| (RM; exit 1)`).
set(ENGLISH_DB "${CMAKE_CURRENT_BINARY_DIR}/english.db")
add_custom_command(
    OUTPUT "${ENGLISH_DB}"
    COMMAND sh -c "rm -f '${ENGLISH_DB}' && LC_NUMERIC=C '${AWK_EXECUTABLE}' -f '${CMAKE_CURRENT_SOURCE_DIR}/english.awk' '${CMAKE_CURRENT_SOURCE_DIR}/wordlist' | '${SQLITE3_EXECUTABLE}' '${ENGLISH_DB}' || { rm -f '${ENGLISH_DB}'; exit 1; }"
    DEPENDS english.awk wordlist
    COMMENT "Generating english.db from wordlist"
    VERBATIM)
add_custom_target(english-db ALL DEPENDS "${ENGLISH_DB}")
install(FILES "${ENGLISH_DB}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/oxpinyin/db")
