import json
import sys

def carica_parametri_topologia(param_file):
    with open(param_file) as f:
        data = json.load(f)
    topo = data["TopologyParams"]
    return {
        "num_groups": topo["num_groups"],
        "spine_routers": topo["spine_routers"],
        "leaf_routers": topo["leaf_routers"],
        "hosts_per_router": topo["hosts_per_router"],
        "chunk_size": topo["chunk_size"]
    }

def check_and_analyze_teccl_output(param_file, schedule_file):
    # === CARICA PARAMETRI TOPOLOGIA ===
    params = carica_parametri_topologia(param_file)
    num_groups = params["num_groups"]
    spine_routers = params["spine_routers"]
    leaf_routers = params["leaf_routers"]
    hosts_per_router = params["hosts_per_router"]
    chunk_size = params["chunk_size"]

    # === CARICAMENTO FILE SCHEDULE ===
    with open(schedule_file) as f:
        data = json.load(f)

    # === STATISTICHE BASE ===
    print("=== Analisi Output TECCL ===")
    print(f"Durata Epoch: {data['1-Epoch_Duration']}")
    print(f"Durata Epoch Attesa: {data['2-Expected_Epoch_Duration']}")
    print(f"Numero Epoch Richiesti: {data['3-Epochs_Required']}")
    print(f"Tempo totale completamento collettiva: {data['4-Collective_Finish_Time']}")
    print(f"Algo_Bandwidth: {data['5-Algo_Bandwidth']} chunk/unità di tempo")

    # === ANALISI TEMPI DI TRASFERIMENTO E VERIFICA ALLGATHER ===
    demand_met = data["6-Demand_Met"]
    gpu_list = list(demand_met.keys())
    tempi = []
    errori = []

    # Determina l'elenco dei chunk da una entry valida
    chunks_set = set()
    for src in gpu_list:
        for dst in demand_met[src]:
            chunks_set.update(demand_met[src][dst].keys())
        break
    chunk_list = list(chunks_set)

    # Analisi e verifica chunk allgather
    missing = []
    for dst in gpu_list:
        for src in gpu_list:
            if src == dst:
                continue
            if src not in demand_met or dst not in demand_met[src]:
                missing.append(f"Manca la comunicazione da {src} a {dst}")
                continue
            chunks = demand_met[src][dst]
            for chunk in chunk_list:
                if chunk not in chunks:
                    missing.append(f"Manca {chunk} da {src} a {dst}")
                else:
                    t = chunks[chunk]
                    if not isinstance(t, int) or t < 0:
                        errori.append(f"Tempo non valido per {src}->{dst} {chunk}: {t}")
                    else:
                        tempi.append(t)

    if tempi:
        print("\nStatistiche tempi trasferimento chunk tra GPU:")
        print(f"Minimo: {min(tempi)}, Massimo: {max(tempi)}, Media: {sum(tempi)//len(tempi)}")
    else:
        print("Nessun tempo di trasferimento valido trovato.")

    if errori:
        print("\n⚠️  Anomalie di tempo rilevate:")
        for err in errori:
            print(f"- {err}")

    if missing:
        print("\n❌ Mancano alcune comunicazioni/chunk (Allgather INCOMPLETA):")
        for m in missing:
            print("-", m)
    else:
        print("\n✅ Tutti i chunk sono stati inviati e ricevuti correttamente per ogni coppia di GPU (Allgather OK).")

    # === DIAGNOSTICA: CONFRONTO IN TUTTE LE UNITA' ===

    algo_bandwidth = data["5-Algo_Bandwidth"]

    # 1. Banda teorica spine in chunk/unità di tempo
    speed_spine_chunk = 80 / chunk_size
    total_spine_links = spine_routers * num_groups
    max_aggregata_chunk = total_spine_links * speed_spine_chunk

    # 2. Banda teorica spine in byte/unità di tempo
    speed_spine_bytes = 80
    max_aggregata_bytes = total_spine_links * speed_spine_bytes

    # 3. Algo_Bandwidth effettivo in byte/unità di tempo
    algo_bandwidth_bytes = algo_bandwidth * chunk_size

    # 4. Efficienze
    eff_chunk = (algo_bandwidth / max_aggregata_chunk) * 100 if max_aggregata_chunk > 0 else 0
    eff_bytes = (algo_bandwidth_bytes / max_aggregata_bytes) * 100 if max_aggregata_bytes > 0 else 0

    print("\n=== Diagnostica confronto banda ===")
    print(f"Parametri topologia:")
    print(f"  Gruppi: {num_groups}, Spine: {spine_routers}, Leaf: {leaf_routers}, Host per router: {hosts_per_router}, Chunk size: {chunk_size}")
    print(f"  Capacità di un link spine: {speed_spine_chunk:.6f} chunk/unità di tempo | {speed_spine_bytes} byte/unità di tempo")
    print(f"  Link spine totali: {total_spine_links}")
    print(f"  Banda massima aggregata (spine): {max_aggregata_chunk:.6f} chunk/unità di tempo | {max_aggregata_bytes} byte/unità di tempo\n")

    print(f"Algo_Bandwidth effettivo: {algo_bandwidth:.6f} chunk/unità di tempo | {algo_bandwidth_bytes:.2f} byte/unità di tempo\n")

    print(f"Efficienza rispetto alla banda aggregata spine:")
    print(f"  In chunk: {eff_chunk:.2f}%")
    print(f"  In byte:  {eff_bytes:.2f}%")

    # === NUOVA SEZIONE: CONFRONTO CON LA CAPACITÀ TOTALE DELLA RETE (LEAF + SPINE) ===
    # Capacità di ogni link leaf: 220 byte/unità di tempo
    speed_leaf_bytes = 220

    # Numero totale di link host-leaf: ogni leaf router collega hosts_per_router host, per ogni gruppo
    num_leaf_host_links = leaf_routers * num_groups * hosts_per_router

    # Numero totale di link leaf-spine: ogni leaf router collega tutti gli spine router dello stesso gruppo, per ogni host
    num_leaf_spine_links = leaf_routers * spine_routers * num_groups * hosts_per_router

    # Totale link leaf (host-leaf + leaf-spine)
    total_leaf_links = num_leaf_host_links + num_leaf_spine_links

    # Capacità aggregata leaf (byte/unità di tempo)
    max_aggregata_leaf_bytes = total_leaf_links * speed_leaf_bytes

    # Capacità aggregata totale rete (leaf + spine)
    max_aggregata_totale_bytes = max_aggregata_leaf_bytes + max_aggregata_bytes

    # Efficienza rispetto alla rete totale
    eff_totale = (algo_bandwidth_bytes / max_aggregata_totale_bytes) * 100 if max_aggregata_totale_bytes > 0 else 0

    print(f"\n--- Confronto con la capacità aggregata TOTALE della rete (leaf + spine) ---")
    print(f"Numero link leaf host: {num_leaf_host_links}")
    print(f"Numero link leaf-spine: {num_leaf_spine_links}")
    print(f"Totale link leaf: {total_leaf_links}")
    print(f"Banda massima aggregata leaf: {max_aggregata_leaf_bytes} byte/unità di tempo")
    print(f"Banda massima aggregata rete (leaf + spine): {max_aggregata_totale_bytes} byte/unità di tempo")
    print(f"Efficienza rispetto alla rete totale: {eff_totale:.2f}%")

    # Diagnosi automatica
    if eff_chunk > 120:
        print("\n⚠️  L'efficienza in chunk/unità di tempo supera il 100%: probabilmente Algo_Bandwidth è espresso in byte/unità di tempo o include traffico non solo sui link spine.")
    elif eff_bytes > 120:
        print("\n⚠️  L'efficienza in byte/unità di tempo supera il 100%: Algo_Bandwidth non è confrontabile direttamente con la sola capacità dei link spine. Forse include anche traffico leaf o intra-gruppo.")
    else:
        print("\nℹ️  Le efficienze calcolate sono plausibili. Le unità di misura sembrano coerenti.")

    print("\nSuggerimenti per la diagnosi:")
    print("- Se efficienza > 100%, controlla se Algo_Bandwidth include traffico intra-gruppo o su link diversi dagli spine.")
    print("- Se vuoi un confronto realistico, calcola la capacità aggregata di tutti i link della rete (leaf + spine).")
    print("- Oppure, modifica il calcolo di Algo_Bandwidth per considerare solo il traffico sui link spine.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Uso: python3 calcolaEfficienza.py <file_parametri.json> <file_output.json>")
        sys.exit(1)
    check_and_analyze_teccl_output(sys.argv[1], sys.argv[2])
