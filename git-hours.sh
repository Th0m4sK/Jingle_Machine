#!/bin/bash
# Schätzt Arbeitszeit anhand von Git-Commits
# Regel: Commits < 2h Abstand = durchgehende Arbeit, sonst +30min pro Session-Start

git log --format="%at" --reverse | awk '
BEGIN { total=0; threshold=7200; prev=0 }
{
    if (prev > 0) {
        diff = $1 - prev
        if (diff < threshold) total += diff
        else total += 1800
    } else {
        total += 1800
    }
    prev = $1
}
END {
    h = int(total/3600)
    m = int((total%3600)/60)
    printf "Geschätzte Arbeitszeit: %dh %dmin\n", h, m
    printf "Basierend auf %d Commits\n", NR
}'
