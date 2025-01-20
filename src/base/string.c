internal b32 char_is_upper(u8 c) {
    return 'A' <= c && c <= 'Z';
}

internal b32 char_is_lower(u8 c) {
    return 'a' <= c && c <= 'z';
}

internal u8 char_to_lower(u8 c) {
    if (char_is_upper(c)) {
        c += ('a' - 'A');
    }
    return c;
}

internal u8 char_to_upper(u8 c) {
    if (char_is_lower(c)) {
        c += ('A' - 'a');
    }
    return c;
}

internal u64 cstring8_length(u8 *c) {
    u8 *p = c;
    for (; *p != 0; ++p) {
    }
    return p - c;
}

internal string8 str8(u8 *str, u64 size) {
    return (string8){.str = str, .size = size};
}

internal string8 str8_range(u8 *first, u8 *one_past_last) {
    return (string8){.str = first, .size = (u64)(one_past_last - first)};
}

internal string8 str8_zero(void) {
    return (string8){0};
}

internal string8 str8_cstring(char *c) {
    return (string8){.str = (u8 *)c, .size = cstring8_length((u8 *)c)};
}

internal rng1u64 rng_1u64(u64 min, u64 max) {
    rng1u64 r = {.min = min, .max = max};
    if (r.min > r.max) {
        SWAP(u64, r.min, r.max);
    }
    return r;
}

internal u64 dim_1u64(rng1u64 r) {
    return r.max > r.min ? (r.max - r.min) : 0;
}

internal b32 str8_match(string8 a, string8 b, string_match_flags flags) {
    b32 result = 0;
    if (a.size == b.size && flags == 0) {
        result = MEMORY_MATCH(a.str, b.str, b.size);
    } else if (a.size == b.size || (flags & STRING_MATCH_FLAGS_RIGHT_SIDE_SLOPPY)) {
        b32 case_insensitive = (flags & STRING_MATCH_FLAGS_CASE_INSENSITIVE);
        u64 size = MIN(a.size, b.size);
        result = 1;
        for (u64 i = 0; i < size; ++i) {
            u8 at = a.str[i];
            u8 bt = b.str[i];
            if (case_insensitive) {
                at = char_to_upper(at);
                bt = char_to_upper(bt);
            }
            if (at != bt) {
                result = 0;
                break;
            }
        }
    }
    return result;
}

internal u64 str8_find_needle(string8 string, u64 start_pos, string8 needle, string_match_flags flags) {
    u8 *p = string.str + start_pos;
    u64 stop_offset = MAX(string.size + 1, needle.size) - needle.size;
    u8 *stop_p = string.str + stop_offset;
    if (needle.size > 0) {
        u8 *string_opl = string.str + string.size;
        string8 needle_tail = str8_skip(needle, 1);
        string_match_flags adjusted_flags = flags | STRING_MATCH_FLAGS_RIGHT_SIDE_SLOPPY;
        u8 needle_first_char_adjusted = needle.str[0];
        if (adjusted_flags & STRING_MATCH_FLAGS_CASE_INSENSITIVE) {
            needle_first_char_adjusted = char_to_upper(needle_first_char_adjusted);
        }
        for (; p < stop_p; ++p) {
            u8 haystack_char_adjusted = *p;
            if (adjusted_flags & STRING_MATCH_FLAGS_CASE_INSENSITIVE) {
                haystack_char_adjusted = char_to_upper(haystack_char_adjusted);
            }
            if (haystack_char_adjusted == needle_first_char_adjusted) {
                if (str8_match(str8_range(p + 1, string_opl), needle_tail, adjusted_flags)) {
                    break;
                }
            }
        }
    }
    u64 result = string.size;
    if (p < stop_p) {
        result = (u64)(p - string.str);
    }
    return result;
}

internal u64 str8_find_needle_reverse(string8 string, u64 start_pos, string8 needle, string_match_flags flags) {
    u64 result = 0;
    for (s64 i = string.size - start_pos - needle.size; i >= 0; --i) {
        string8 haystack = str8_substr(string, rng_1u64(i, i + needle.size));
        if (str8_match(haystack, needle, flags)) {
            result = (u64)i + needle.size;
            break;
        }
    }
    return result;
}

internal string8 str8_substr(string8 str, rng1u64 range) {
    range.min = MIN(range.min, str.size);
    range.max = MIN(range.max, str.size);
    str.str += range.min;
    str.size = dim_1u64(range);
    return str;
}

internal string8 str8_prefix(string8 str, u64 size) {
    str.size = MIN(size, str.size);
    return str;
}

internal string8 str8_skip(string8 str, u64 amt) {
    amt = MIN(amt, str.size);
    str.str += amt;
    str.size -= amt;
    return str;
}

internal string8 str8_postfix(string8 str, u64 size) {
    size = MIN(size, str.size);
    str.str = str.str + str.size - size;
    str.size = size;
    return str;
}

internal string8 push_str8_cat(arena *a, string8 s1, string8 s2) {
    string8 str;
    str.size = s1.size + s2.size;
    str.str = push_array_no_zero(a, u8, str.size + 1);
    memcpy(str.str, s1.str, s1.size);
    memcpy(str.str + s1.size, s2.str, s2.size);
    str.str[str.size] = 0;
    return str;
}

internal string8 push_str8_copy(arena *a, string8 s) {
    string8 str;
    str.size = s.size;
    str.str = push_array_no_zero(a, u8, str.size + 1);
    memcpy(str.str, s.str, s.size);
    str.str[str.size] = 0;
    return str;
}

internal string8 push_str8fv(arena *a, char *fmt, va_list args) {
    va_list args2;
    va_copy(args2, args);
    u32 needed_bytes = vsnprintf(0, 0, fmt, args) + 1;
    string8 result = {.str = push_array_no_zero(a, u8, needed_bytes),
                      .size = (u64)vsnprintf((char *)result.str, needed_bytes, fmt, args2)};
    result.str[result.size] = 0;
    va_end(args2);
    return result;
}

internal string8 push_str8f(arena *a, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    string8 result = push_str8fv(a, fmt, args);
    va_end(args);
    return result;
}

internal b32 str8_is_integer(string8 string, u32 radix) {
    b32 result = 0;
    if (string.size > 0) {
        if (1 < radix && radix <= 16) {
            result = 1;
            for (u64 i = 0; i < string.size; ++i) {
                u8 c = string.str[i];
                if (!(c < 0x80) || integer_symbol_reverse[c] >= radix) {
                    result = 0;
                    break;
                }
            }
        }
    }
    return result;
}

internal u64 u64_from_str8(string8 string, u32 radix) {
    u64 x = 0;
    if (1 < radix && radix <= 16) {
        for (u64 i = 0; i < string.size; ++i) {
            x *= radix;
            x += integer_symbol_reverse[string.str[i] & 0x7F];
        }
    }
    return x;
}

internal b32 try_u64_from_str8_c_rules(string8 string, u64 *x) {
    b32 is_integer = 0;
    if (str8_is_integer(string, 10)) {
        is_integer = 1;
        *x = u64_from_str8(string, 10);
    } else {
        string8 hex_string = str8_skip(string, 2);
        if (str8_match(str8_prefix(string, 2), str8_lit("0x"), 0) && str8_is_integer(hex_string, 0x10)) {
            is_integer = 1;
            *x = u64_from_str8(hex_string, 0x10);
        } else if (str8_match(str8_prefix(string, 2), str8_lit("0b"), 0) && str8_is_integer(hex_string, 2)) {
            is_integer = 1;
            *x = u64_from_str8(hex_string, 2);
        } else {
            string8 oct_string = str8_skip(string, 1);
            if (str8_match(str8_prefix(string, 1), str8_lit("0"), 0) && str8_is_integer(hex_string, 010)) {
                is_integer = 1;
                *x = u64_from_str8(oct_string, 010);
            }
        }
    }
    return is_integer;
}

internal string8 str8_from_u64(arena *a, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator) {
    string8 result = {0};
    {
        string8 prefix = {0};
        switch (radix) {
            case 16: {
                prefix = str8_lit("0x");
            } break;
            case 8: {
                prefix = str8_lit("0o");
            } break;
            case 2: {
                prefix = str8_lit("0b");
            } break;
            default:
                break;
        }
        u8 digit_group_size = 3;
        switch (radix) {
            default:
                break;
            case 2:
            case 8:
            case 16: {
                digit_group_size = 4;
            } break;
        }
        u64 needed_leading_0s = 0;
        {
            u64 needed_digits = 1;
            {
                u64 u64_reduce = value;
                for (;;) {
                    u64_reduce /= radix;
                    if (u64_reduce == 0) {
                        break;
                    }
                    ++needed_digits;
                }
            }
            needed_leading_0s = (min_digits > needed_digits) ? min_digits - needed_digits : 0;
            u64 needed_separators = 0;
            if (digit_group_separator != 0) {
                needed_separators = (needed_digits + needed_leading_0s) / digit_group_size;
                if (needed_separators > 0 && (needed_digits + needed_leading_0s) % digit_group_size == 0) {
                    --needed_separators;
                }
            }
            result.size = prefix.size + needed_leading_0s + needed_separators + needed_digits;
            result.str = push_array_no_zero(a, u8, result.size + 1);
            result.str[result.size] = 0;
        }
        {
            u64 u64_reduce = value;
            u64 digits_until_separator = digit_group_size;
            for (u64 i = 0; i < result.size; ++i) {
                if (digits_until_separator == 0 && digit_group_separator != 0) {
                    result.str[result.size - i - 1] = digit_group_separator;
                    digits_until_separator = digit_group_size + 1;
                } else {
                    result.str[result.size - i - 1] = char_to_lower(integer_symbols[u64_reduce % radix]);
                    u64_reduce /= radix;
                }
                --digits_until_separator;
                if (u64_reduce == 0) {
                    break;
                }
            }
            for (u64 leading_0_idx = 0; leading_0_idx < needed_leading_0s; ++leading_0_idx) {
                result.str[prefix.size + leading_0_idx] = '0';
            }
        }
        if (prefix.size != 0) {
            memcpy(result.str, prefix.str, prefix.size);
        }
    }
    return result;
}

internal string8node *str8_list_push_node_set_string(string8list *list, string8node *node, string8 string) {
    SLL_QUEUE_PUSH(list->first, list->last, node);
    ++list->node_count;
    list->total_size += string.size;
    node->string = string;
    return node;
}

internal string8node *str8_list_push(arena *a, string8list *list, string8 string) {
    string8node *node = push_array_no_zero(a, string8node, 1);
    str8_list_push_node_set_string(list, node, string);
    return node;
}

internal string8list str8_split(arena *a, string8 string, u8 *split_chars, u64 split_char_count,
                                string_split_flags flags) {
    string8list list = {0};
    b32 keep_empties = (flags & STRING_SPLIT_FLAGS_KEEP_EMPTY);
    u8 *ptr = string.str;
    u8 *opl = string.str + string.size;
    for (; ptr < opl;) {
        u8 *first = ptr;
        for (; ptr < opl; ++ptr) {
            u8 c = *ptr;
            b32 is_split = 0;
            for (u64 i = 0; i < split_char_count; ++i) {
                if (split_chars[i] == c) {
                    is_split = 1;
                    break;
                }
            }
            if (is_split) {
                break;
            }
        }
        string8 string = str8_range(first, ptr);
        if (keep_empties || string.size > 0) {
            str8_list_push(a, &list, string);
        }
        ++ptr;
    }
    return list;
}

internal string8 str8_list_join(arena *a, string8list *list, string_join *optional_params) {
    string_join join = {{0}};
    if (optional_params != 0) {
        memcpy(&join, optional_params, sizeof(join));
    }
    u64 sep_count = 0;
    if (list->node_count > 0) {
        sep_count = list->node_count - 1;
    }
    string8 result;
    result.size = join.pre.size + join.post.size + sep_count * join.sep.size + list->total_size;
    u8 *ptr = result.str = push_array_no_zero(a, u8, result.size + 1);
    memcpy(ptr, join.pre.str, join.pre.size);
    ptr += join.pre.size;
    for (string8node *node = list->first; node != 0; node = node->next) {
        memcpy(ptr, node->string.str, node->string.size);
        ptr += node->string.size;
        if (node->next != 0) {
            memcpy(ptr, join.sep.str, join.sep.size);
            ptr += join.sep.size;
        }
    }
    memcpy(ptr, join.post.str, join.post.size);
    ptr += join.post.size;
    *ptr = 0;
    return result;
}
