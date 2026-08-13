#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

run_case() {
    name="$1"
    ./sangen "examples/$name.kbn" > "test/.actual_$name.txt"
    diff -u "test/expected/$name.txt" "test/.actual_$name.txt"

    ./sangen "examples/$name.kbn" --文樹 > /dev/null
    ./sangen "examples/$name.kbn" --詞 > /dev/null
    ./sangen "examples/$name.kbn" --字碼 > /dev/null

    ./sangen "examples/$name.kbn" --譯=c > "test/.$name.c"
    cc -std=c99 -Wall -Wextra -o "test/.$name"_c "test/.$name.c"
    "./test/.$name"_c > "test/.actual_${name}_c.txt"
    diff -u "test/expected/$name.txt" "test/.actual_${name}_c.txt"
}

run_error() {
    name="$1"
    if ./sangen "test/errors/$name.kbn" > "test/.actual_${name}_out.txt" 2> "test/.actual_${name}_err.txt"; then
        echo "expected failure: $name" >&2
        exit 1
    fi
    diff -u "test/expected/errors/$name.txt" "test/.actual_${name}_err.txt"
}

run_error_path() {
    name="$1"
    path="$2"
    if ./sangen "$path" > "test/.actual_${name}_out.txt" 2> "test/.actual_${name}_err.txt"; then
        echo "expected failure: $name" >&2
        exit 1
    fi
    diff -u "test/expected/errors/$name.txt" "test/.actual_${name}_err.txt"
}

run_error_backend() {
    name="$1"
    ./sangen "test/errors/$name.kbn" --譯=c > "test/.error_$name.c"
    cc -std=c99 -Wall -Wextra -o "test/.error_${name}_c" "test/.error_$name.c"
    if "./test/.error_${name}_c" > "test/.actual_${name}_c_out.txt" 2> "test/.actual_${name}_c_err.txt"; then
        echo "expected generated C failure: $name" >&2
        exit 1
    fi
    diff -u "test/expected/errors/$name.txt" "test/.actual_${name}_c_err.txt"
}

run_error_arg() {
    name="$1"
    arg="$2"
    if ./sangen "test/errors/$name.kbn" "$arg" > "test/.actual_${name}_${arg}_out.txt" 2> "test/.actual_${name}_${arg}_err.txt"; then
        echo "expected failure: $name $arg" >&2
        exit 1
    fi
    diff -u "test/expected/errors/$name.txt" "test/.actual_${name}_${arg}_err.txt"
}

run_case fizzbuzz
run_case double
run_case gcd
run_case compare
run_case numerals
run_case ops
run_case string
run_case compat
run_case context
run_case reten
run_case v2
run_case reten_compat
./sangen examples/reten_compat.kbn --校=訓點 > test/.actual_reten_compat_lint.txt
diff -u test/expected/debug/reten_compat_lint.txt test/.actual_reten_compat_lint.txt

./sangen examples/fizzbuzz.kbn --詞 > test/.actual_fizzbuzz_tokens.txt
diff -u test/expected/debug/fizzbuzz_tokens.txt test/.actual_fizzbuzz_tokens.txt
./sangen examples/fizzbuzz.kbn --文樹 > test/.actual_fizzbuzz_ast.txt
diff -u test/expected/debug/fizzbuzz_ast.txt test/.actual_fizzbuzz_ast.txt

./sangen examples/compat.kbn --校 > test/.actual_compat_lint.txt
diff -u test/expected/debug/compat_lint.txt test/.actual_compat_lint.txt
if ./sangen examples/compat.kbn --正格 > test/.actual_compat_strict_out.txt 2> test/.actual_compat_strict_err.txt; then
    echo "expected 正格 failure: compat" >&2
    exit 1
fi
diff -u test/expected/debug/compat_lint.txt test/.actual_compat_strict_err.txt
./sangen examples/compat.kbn --整 > test/.actual_compat_rewrite.kbn
diff -u test/expected/debug/compat_rewrite.kbn test/.actual_compat_rewrite.kbn
./sangen test/.actual_compat_rewrite.kbn > test/.actual_compat_rewrite_out.txt
diff -u test/expected/compat.txt test/.actual_compat_rewrite_out.txt

./sangen examples/reten.kbn --詞 > test/.actual_reten_tokens.txt
diff -u test/expected/debug/reten_tokens.txt test/.actual_reten_tokens.txt
./sangen examples/reten.kbn --讀順 > test/.actual_reten_reading.txt
diff -u test/expected/debug/reten_reading.txt test/.actual_reten_reading.txt
./sangen examples/reten.kbn --整=表記 > test/.actual_reten_surface.kbn
diff -u test/expected/debug/reten_surface.kbn test/.actual_reten_surface.kbn

if ./sangen examples/reten.kbn --整 > test/.actual_reten_rewrite.kbn 2> test/.actual_reten_rewrite_err.txt; then
    echo "expected 整 failure: reten" >&2
    exit 1
fi
printf '%s\n' '返點有り 整文すれば失所す' > test/.expected_reten_rewrite_err.txt
diff -u test/.expected_reten_rewrite_err.txt test/.actual_reten_rewrite_err.txt

run_error invalid_char
run_error missing_then
run_error div_zero
run_error undefined_var
run_error bad_numeral
run_error reversed_range
run_error overflow
run_error duplicate_func
run_error duplicate_param
run_error nested_func
run_error reten_no_before
run_error reten_no_after
run_error reten_duplicate
run_error reten_cross_line
run_error reten_punct_boundary
run_error reten_number_split
run_error reten_word_split
run_error reten_name_split
run_error v2_arg_count
run_error quoted_string
run_error undefined_func
run_error unclosed_text
run_error unclosed_note
printf '\300\257' > test/.invalid_utf8.kbn
run_error_path invalid_utf8 test/.invalid_utf8.kbn
run_error_path bad_ext test/errors/bad_ext.txt

run_error_backend div_zero
run_error_backend undefined_var
run_error_backend reversed_range
run_error_backend overflow
run_error_arg undefined_func --譯=c

./sangen test/errors/missing_then.kbn --詞 > /dev/null

echo "ok"
