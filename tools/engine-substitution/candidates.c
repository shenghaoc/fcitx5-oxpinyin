#include <pinyin.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    pinyin_context_t *context = pinyin_init(argv[1], argv[2]);
    if (!context)
        return 1;
    /* Match the adapter's default incomplete/dynamic-adjust configuration. */
    pinyin_set_options(context, DYNAMIC_ADJUST | PINYIN_INCOMPLETE |
                                    ZHUYIN_INCOMPLETE | USE_DIVIDED_TABLE |
                                    USE_RESPLIT_TABLE);
    pinyin_instance_t *instance = pinyin_alloc_instance(context);
    pinyin_parse_more_full_pinyins(instance, argv[3]);
    pinyin_guess_sentence(instance);
    pinyin_guess_candidates(instance, 0, SORT_BY_PHRASE_LENGTH);
    guint count = 0;
    pinyin_get_n_candidate(instance, &count);
    printf("%s parsed=%zu count=%u\n", argv[3],
           pinyin_get_parsed_input_length(instance), count);
    for (guint i = 0; i < count; ++i) {
        lookup_candidate_t *candidate = NULL;
        const gchar *text = NULL;
        if (!pinyin_get_candidate(instance, i, &candidate) ||
            !pinyin_get_candidate_string(instance, candidate, &text))
            return 1;
        printf("%u\t%s\n", i, text);
    }
    pinyin_free_instance(instance);
    pinyin_fini(context);
    return 0;
}
