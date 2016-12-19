exec 8<>/dev/leftpad
sed 's#\\#\\\\#g' | (
        while read line; do
            printf '%s\n' "$line" >&8
            head -n 1 <&8
        done
    )
