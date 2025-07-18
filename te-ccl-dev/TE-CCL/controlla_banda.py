import json
import sys
from collections import defaultdict

def somma_traffico(demand_met):
    totale = 0
    for src in demand_met:
        for dst in demand_met[src]:
            for chunk in demand_met[src][dst]:
                totale += demand_met[src][dst][chunk]
    return totale

def conta_gpu(demand_met):
    return len(demand_met)

def traffico_per_gpu(demand_met):
    inviato = defaultdict(int)
    ricevuto = defaultdict(int)
    for src in demand_met:
        for dst in demand_met[src]:
            for chunk in demand_met[src][dst]:
                val = demand_met[src][dst][chunk]
                inviato[src] += val
                ricevuto[dst] += val
    return inviato, ricevuto

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Uso: python {sys.argv[0]} <file_json>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        data = json.load(f)

    demand_met = data["6-Demand_Met"]
    finish_time = data["4-Collective_Finish_Time"]
    banda_teorica = 0.23438  # inserisci qui il valore calcolato in precedenza

    num_gpu = conta_gpu(demand_met)
    traffico_totale = somma_traffico(demand_met)
    traffico_totale_corretto = traffico_totale / 2  # rimuovi doppioni

    # Banda media per GPU (traffico totale corretto / tempo / num_gpu)
    banda_media_per_gpu = traffico_totale_corretto / finish_time / num_gpu

    print(f"Numero GPU: {num_gpu}")
    print(f"Traffico totale trasferito (corretto): {traffico_totale_corretto}")
    print(f"Tempo totale comunicazione: {finish_time}")
    print(f"Banda media effettiva per GPU: {banda_media_per_gpu:.4f}")
    print(f"Banda teorica massima: {banda_teorica}")
    print(f"Efficienza rispetto alla banda teorica: {banda_media_per_gpu / banda_teorica:.2f}\n")

    # Analisi traffico per GPU
    inviato, ricevuto = traffico_per_gpu(demand_met)
    print("Traffico inviato e ricevuto per GPU:")
    for gpu in sorted(inviato.keys(), key=lambda x: int(x.split()[-1])):
        print(f"{gpu}: inviato={inviato[gpu]}, ricevuto={ricevuto[gpu]}")

    max_inviato = max(inviato.values())
    max_ricevuto = max(ricevuto.values())
    print(f"\nMassimo traffico inviato da una GPU: {max_inviato}")
    print(f"Massimo traffico ricevuto da una GPU: {max_ricevuto}")
    print(f"Banda media massima per una GPU (inviato): {max_inviato / finish_time:.4f}")
    print(f"Banda media massima per una GPU (ricevuto): {max_ricevuto / finish_time:.4f}")
    print(f"Efficienza massima rispetto alla banda teorica (inviato): {(max_inviato / finish_time) / banda_teorica:.2f}")
    print(f"Efficienza massima rispetto alla banda teorica (ricevuto): {(max_ricevuto / finish_time) / banda_teorica:.2f}")
