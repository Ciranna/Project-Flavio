import json
import sys
from math import comb

def calculate_max_bandwidth(num_groups, leaf_routers, spine_routers, hosts_per_router, chunk_size):
    speed_leaf = 220 / chunk_size
    speed_spine = 80 / chunk_size

    # Totale nodi
    total_hosts = num_groups * (leaf_routers + spine_routers) * hosts_per_router

    # Link host-host intra leaf router
    host_host_links_per_leaf = hosts_per_router * (hosts_per_router - 1)  # direzionale
    total_host_host_links = host_host_links_per_leaf * leaf_routers * num_groups

    # Link leaf-spine intra gruppo (bipartito completo)
    leaf_spine_links_per_group = leaf_routers * spine_routers
    total_leaf_spine_links = leaf_spine_links_per_group * num_groups

    # Link spine-spine inter gruppo (fully connected tra gruppi)
    inter_group_pairs = comb(num_groups, 2)
    total_spine_spine_links = spine_routers * spine_routers * inter_group_pairs

    # Banda totale teorica (somma capacità link moltiplicata per velocità)
    bandwidth_host_host = total_host_host_links * speed_leaf
    bandwidth_leaf_spine = total_leaf_spine_links * speed_leaf
    bandwidth_spine_spine = total_spine_spine_links * speed_spine

    total_bandwidth = bandwidth_host_host + bandwidth_leaf_spine + bandwidth_spine_spine

    print(f"Bandwidth Host-Host (leaf): {bandwidth_host_host:.5f}")
    print(f"Bandwidth Leaf-Spine (intra group): {bandwidth_leaf_spine:.5f}")
    print(f"Bandwidth Spine-Spine (inter group): {bandwidth_spine_spine:.5f}")
    print(f"Total theoretical bandwidth: {total_bandwidth:.5f}")

    return total_bandwidth

def main(json_file):
    with open(json_file, 'r') as f:
        data = json.load(f)

    topo = data.get("TopologyParams", {})
    num_groups = topo.get("num_groups")
    leaf_routers = topo.get("leaf_routers")
    spine_routers = topo.get("spine_routers")
    hosts_per_router = topo.get("hosts_per_router")
    chunk_size = topo.get("chunk_size")

    if None in (num_groups, leaf_routers, spine_routers, hosts_per_router, chunk_size):
        print("Errore: parametri topologia mancanti nel file JSON")
        sys.exit(1)

    calculate_max_bandwidth(num_groups, leaf_routers, spine_routers, hosts_per_router, chunk_size)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Uso: python {sys.argv[0]} <file_input.json>")
        sys.exit(1)
    main(sys.argv[1])
