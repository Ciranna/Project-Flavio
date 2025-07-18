#!/usr/bin/env python3
import json
import sys

def check_and_analyze_teccl_output(file_path):
    try:
        with open(file_path) as f:
            data = json.load(f)
    except Exception as e:
        print(f"Errore nella lettura del file: {e}")
        return

    # Verifica presenza campi chiave
    required_keys = [
        "1-Epoch_Duration", "2-Expected_Epoch_Duration",
        "3-Epochs_Required", "4-Collective_Finish_Time",
        "5-Algo_Bandwidth", "6-Demand_Met"
    ]
    for key in required_keys:
        if key not in data:
            print(f"Campo mancante: {key}")
            return

    print("=== Analisi Output TECCL ===")
    print(f"Durata Epoch: {data['1-Epoch_Duration']}")
    print(f"Durata Epoch Attesa: {data['2-Expected_Epoch_Duration']}")
    print(f"Numero Epoch Richiesti: {data['3-Epochs_Required']}")
    print(f"Tempo totale completamento collettiva: {data['4-Collective_Finish_Time']}")
    print(f"Algo_Bandwidth: {data['5-Algo_Bandwidth']} chunk/unità di tempo")

    demand_met = data["6-Demand_Met"]
    gpu_list = list(demand_met.keys())
    num_gpus = len(gpu_list)
    tempi = []
    errori = []

    # Controllo: tutte le GPU comunicano con tutte le altre?
    for src in gpu_list:
        dests = demand_met[src]
        if len(dests) != num_gpus - 1:
            errori.append(f"La GPU {src} comunica solo con {len(dests)} GPU invece di {num_gpus-1}")
        for dst in gpu_list:
            if dst == src:
                continue
            if dst not in dests:
                errori.append(f"Manca la comunicazione da {src} a {dst}")
                continue
            chunks = dests[dst]
            if not isinstance(chunks, dict) or len(chunks) == 0:
                errori.append(f"Nessun chunk per {src} -> {dst}")
                continue
            for chunk_id, t in chunks.items():
                if not isinstance(t, int) or t < 0:
                    errori.append(f"Tempo non valido per {src}->{dst} {chunk_id}: {t}")
                else:
                    tempi.append(t)

    if tempi:
        print("\nStatistiche tempi trasferimento chunk tra GPU:")
        print(f"Minimo: {min(tempi)}, Massimo: {max(tempi)}, Media: {sum(tempi)//len(tempi)}")
    else:
        print("Nessun tempo di trasferimento valido trovato.")

    if errori:
        print("\n⚠️  Anomalie rilevate:")
        for err in errori:
            print(f"- {err}")
    else:
        print("\nOutput TECCL formalmente corretto e coerente.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Uso: ./analizza_teccl.py <file_output.json>")
        sys.exit(1)
    check_and_analyze_teccl_output(sys.argv[1])
    