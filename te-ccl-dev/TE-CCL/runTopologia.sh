#!/bin/bash

if [ $# -lt 3 ]; then
  echo "Usage: $0 file_json parametro valore [parametro valore ...]"
  exit 1
fi

FILE=$1
shift 1

if [ ! -f "$FILE" ]; then
  echo "File $FILE non trovato!"
  exit 1
fi

cp "$FILE" "${FILE}.bak"

while [[ $# -gt 0 ]]; do
  PARAM=$1
  VAL=$2

  # Modifica regex per riconoscere anche numeri negativi
  if [[ "$VAL" =~ ^-?[0-9]+(\.[0-9]+)?$ ]] || [[ "$VAL" == "true" ]] || [[ "$VAL" == "false" ]]; then
    VAL_FORMATTED=$VAL
  else
    VAL_FORMATTED="\"$VAL\""
  fi

  case $PARAM in
  name)
    JQ_FILTER=".TopologyParams.name = $VAL_FORMATTED"
    ;;
  num_groups)
    JQ_FILTER=".TopologyParams.num_groups = $VAL_FORMATTED"
    ;;
  routers_per_group)
    JQ_FILTER=".TopologyParams.routers_per_group = $VAL_FORMATTED"
    ;;
  hosts_per_router)
    JQ_FILTER=".TopologyParams.hosts_per_router = $VAL_FORMATTED"
    ;;
  chunk_size)
    JQ_FILTER=".TopologyParams.chunk_size = $VAL_FORMATTED"
    ;;
  alpha)
    JQ_FILTER=".TopologyParams.alpha = $VAL"
    ;;
  leaf_routers)
    JQ_FILTER=".TopologyParams.leaf_routers = $VAL_FORMATTED"
    ;;
  spine_routers)
    JQ_FILTER=".TopologyParams.spine_routers = $VAL_FORMATTED"
    ;;
  time_limit)
    JQ_FILTER=".GurobiParams.time_limit = $VAL_FORMATTED"
    ;;
  feasibility_tol)
    JQ_FILTER=".GurobiParams.feasibility_tol = $VAL_FORMATTED"
    ;;
  intfeas_tol)
    JQ_FILTER=".GurobiParams.intfeas_tol = $VAL_FORMATTED"
    ;;
  optimality_tol)
    JQ_FILTER=".GurobiParams.optimality_tol = $VAL_FORMATTED"
    ;;
  output_flag)
    JQ_FILTER=".GurobiParams.output_flag = $VAL_FORMATTED"
    ;;
  log_file)
    JQ_FILTER=".GurobiParams.log_file = $VAL_FORMATTED"
    ;;
  log_to_console)
    JQ_FILTER=".GurobiParams.log_to_console = $VAL_FORMATTED"
    ;;
  mip_gap)
    JQ_FILTER=".GurobiParams.mip_gap = $VAL_FORMATTED"
    ;;
  mip_focus)
    JQ_FILTER=".GurobiParams.mip_focus = $VAL_FORMATTED"
    ;;
  crossover)
    JQ_FILTER=".GurobiParams.crossover = $VAL_FORMATTED"
    ;;
  method)
    JQ_FILTER=".GurobiParams.method = $VAL_FORMATTED"
    ;;
  heuristics)
    JQ_FILTER=".GurobiParams.heuristics = $VAL_FORMATTED"
    ;;
  collective)
    JQ_FILTER=".InstanceParams.collective = $VAL_FORMATTED"
    ;;
  num_chunks)
    JQ_FILTER=".InstanceParams.num_chunks = $VAL_FORMATTED"
    ;;
  epoch_type)
    JQ_FILTER=".InstanceParams.epoch_type = $VAL_FORMATTED"
    ;;
  epoch_duration)
    JQ_FILTER=".InstanceParams.epoch_duration = $VAL_FORMATTED"
    ;;
  num_epochs)
    JQ_FILTER=".InstanceParams.num_epochs = $VAL_FORMATTED"
    ;;
  alpha_threshold)
    JQ_FILTER=".InstanceParams.alpha_threshold = $VAL_FORMATTED"
    ;;
  switch_copy)
    JQ_FILTER=".InstanceParams.switch_copy = $VAL_FORMATTED"
    ;;
  debug)
    JQ_FILTER=".InstanceParams.debug = $VAL_FORMATTED"
    ;;
  debug_output_file)
    JQ_FILTER=".InstanceParams.debug_output_file = $VAL_FORMATTED"
    ;;
  objective_type)
    JQ_FILTER=".InstanceParams.objective_type = $VAL_FORMATTED"
    ;;
  solution_method)
    JQ_FILTER=".InstanceParams.solution_method = $VAL_FORMATTED"
    ;;
  schedule_output_file)
    JQ_FILTER=".InstanceParams.schedule_output_file = $VAL_FORMATTED"
    ;;
  *)
    echo "Parametro non riconosciuto: $PARAM"
    exit 1
    ;;
  esac

  tmpfile=$(mktemp)
  jq "$JQ_FILTER" "$FILE" > "$tmpfile" && mv "$tmpfile" "$FILE"

  shift 2
done

echo "File modificato: $FILE"

# Ora lancio il comando
teccl solve --input_args "$FILE"
