#!/bin/bash

# Controlla che siano passati due file come argomenti
if [ "$#" -ne 2 ]; then
  echo "Uso: $0 file1.json file2.json"
  exit 1
fi

file1="$1"
file2="$2"

# Estrae la sezione "8-Chunk paths" da entrambi i file usando jq
chunk_paths_1=$(jq '.["8-Chunk paths"]' "$file1")
chunk_paths_2=$(jq '.["8-Chunk paths"]' "$file2")

if [ -z "$chunk_paths_1" ] || [ -z "$chunk_paths_2" ]; then
  echo "Errore: una delle sezioni '8-Chunk paths' non è stata trovata."
  exit 2
fi

# Normalizza l'output (ordinando le chiavi) per un confronto affidabile
sorted_1=$(echo "$chunk_paths_1" | jq -S .)
sorted_2=$(echo "$chunk_paths_2" | jq -S .)

# Confronta i due JSON normalizzati
if diff <(echo "$sorted_1") <(echo "$sorted_2") > /dev/null; then
  echo "Le sezioni '8-Chunk paths' sono IDENTICHE."
  exit 0
else
  echo "Le sezioni '8-Chunk paths' SONO DIVERSE."
  echo "Differenze:"
  diff <(echo "$sorted_1") <(echo "$sorted_2")
  exit 3
fi
