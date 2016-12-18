exec 9<>$1
(
    while read line; do
        printf '%s\n' "$line" >&9
        head -n 1 <&9
    done
) < README.md
