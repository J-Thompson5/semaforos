#!/usr/bin/env python3
import os
import sys
import time
import shutil
import signal
import subprocess
import statistics
import csv

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_DIR = os.path.join(BASE_DIR, "bin")
SERVER_NAME = os.environ.get("SERVER_BIN", sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "server_pool")
SERVER_BIN = os.path.join(BIN_DIR, SERVER_NAME)
RESULTS_DIR = os.path.join(BASE_DIR, "results_sem" if "sem" in SERVER_NAME else "results_condvar")
CLIENT_BIN = os.path.join(BIN_DIR, "load_client")

SCENARIOS = [
    (1, 1, 150),
    (1, 2, 150),
    (1, 4, 150),
    (1, 8, 150),
    (2, 1, 150),
    (2, 2, 300),
    (2, 4, 300),
]

REPETITIONS = 5
BASE_PORT = 9100

def has_taskset():
    return shutil.which("taskset") is not None

def run_single_test(cores, threads, packets, port):
    env = os.environ.copy()
    env["SERVER_QUIET"] = "1"

    server_cmd = []
    if has_taskset():
        core_list = ",".join(str(i) for i in range(cores))
        server_cmd.extend(["taskset", "-c", core_list])

    server_cmd.extend([SERVER_BIN, str(port), str(threads), "256"])

    server_proc = subprocess.Popen(
        server_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=True
    )

    time.sleep(0.15)

    client_cmd = [
        CLIENT_BIN,
        "127.0.0.1",
        str(port),
        str(threads),
        str(packets),
        "--total"
    ]

    try:
        client_res = subprocess.run(
            client_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10
        )
    except subprocess.TimeoutExpired:
        print(f"Client timed out for scenario (Cores={cores}, Hilos={threads}, Pkts={packets})")
        server_proc.kill()
        return None

    server_proc.send_signal(signal.SIGINT)
    try:
        server_out, _ = server_proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        server_proc.kill()
        server_out, _ = server_proc.communicate()

    elapsed_s = None
    throughput = None
    latency_ms = None
    completed = 0

    for line in client_res.stdout.splitlines():
        if line.startswith("[METRICS]"):
            parts = line.split()
            for part in parts[1:]:
                k, v = part.split("=")
                if k == "completed":
                    completed = int(v)
                elif k == "elapsed_s":
                    elapsed_s = float(v)
                elif k == "throughput":
                    throughput = float(v)
                elif k == "latency_ms":
                    latency_ms = float(v)

    if elapsed_s is not None and throughput is not None:
        return {
            "completed": completed,
            "elapsed_s": elapsed_s,
            "throughput": throughput,
            "latency_ms": latency_ms
        }
    return None

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)

    print("=================================================================")
    print("   MINI-SERVER PRODUCER-CONSUMER: BENCHMARK Y RECOLECCIÓN       ")
    print("=================================================================")
    print(f"Taskset disponible: {'Sí' if has_taskset() else 'No (macOS Darwin - Planificador nativo)'}")
    print(f"Repeticiones por escenario: {REPETITIONS}")
    print(f"Total de escenarios: {len(SCENARIOS)}")
    print("-----------------------------------------------------------------")

    results_table = []
    current_port = BASE_PORT

    for cores, threads, packets in SCENARIOS:
        print(f"Ejecutando: Cores={cores} | Hilos={threads} | Paquetes={packets} ... ", end="", flush=True)

        samples_time = []
        samples_tput = []
        samples_lat = []
        completed_counts = []

        for r in range(REPETITIONS):
            res = run_single_test(cores, threads, packets, current_port)
            current_port += 1
            if res:
                samples_time.append(res["elapsed_s"])
                samples_tput.append(res["throughput"])
                samples_lat.append(res["latency_ms"])
                completed_counts.append(res["completed"])
            time.sleep(0.05)

        if samples_time:
            mean_time = statistics.mean(samples_time)
            mean_tput = statistics.mean(samples_tput)
            mean_lat = statistics.mean(samples_lat)
            stdev_time = statistics.stdev(samples_time) if len(samples_time) > 1 else 0.0

            row = {
                "cores": cores,
                "hilos": threads,
                "paquetes": packets,
                "paquetes_completados": int(statistics.mean(completed_counts)),
                "tiempo_promedio_s": round(mean_time, 5),
                "tiempo_ms": round(mean_time * 1000.0, 2),
                "desv_est_s": round(stdev_time, 5),
                "throughput_req_s": round(mean_tput, 2),
                "latencia_promedio_ms": round(mean_lat, 4)
            }
            results_table.append(row)
            print(f"Completado (Tiempo: {row['tiempo_ms']} ms | Throughput: {row['throughput_req_s']} req/s)")
        else:
            print("FALLÓ")

    csv_path = os.path.join(RESULTS_DIR, "benchmark_results.csv")
    with open(csv_path, mode="w", newline="", encoding="utf-8") as f:
        fieldnames = [
            "Cantidad de Cores",
            "Cantidad de hilos",
            "Cantidad de paquetes",
            "Paquetes completados",
            "Tiempo promedio (s)",
            "Tiempo promedio (ms)",
            "Desv. Est. (s)",
            "Throughput (req/s)",
            "Latencia promedio (ms)"
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in results_table:
            writer.writerow({
                "Cantidad de Cores": r["cores"],
                "Cantidad de hilos": r["hilos"],
                "Cantidad de paquetes": r["paquetes"],
                "Paquetes completados": r["paquetes_completados"],
                "Tiempo promedio (s)": r["tiempo_promedio_s"],
                "Tiempo promedio (ms)": r["tiempo_ms"],
                "Desv. Est. (s)": r["desv_est_s"],
                "Throughput (req/s)": r["throughput_req_s"],
                "Latencia promedio (ms)": r["latencia_promedio_ms"]
            })

    print(f"\nResultados guardados en: {csv_path}")

    generate_charts(results_table)

    print("\n" + "="*80)
    print("                      TABLA FINAL DE RESULTADOS")
    print("="*80)
    print("| Cantidad de Cores | Cantidad de hilos | Cantidad de paquetes | Tiempo (ms) | Throughput (req/s) | Latencia (ms) |")
    print("|:-----------------:|:-----------------:|:--------------------:|:-----------:|:------------------:|:-------------:|")
    for r in results_table:
        print(f"| {r['cores']:^17} | {r['hilos']:^17} | {r['paquetes']:^20} | {r['tiempo_ms']:^11.2f} | {r['throughput_req_s']:^18.2f} | {r['latencia_promedio_ms']:^13.4f} |")
    print("="*80)

def generate_charts(data):
    try:
        import matplotlib.pyplot as plt
        import numpy as np

        plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
        plt.rcParams.update({'font.size': 11, 'figure.autolayout': True})

        plt.figure(figsize=(9, 5.5))
        core1_data = [d for d in data if d["cores"] == 1]
        core2_data = [d for d in data if d["cores"] == 2]

        if core1_data:
            x1 = [d["hilos"] for d in core1_data]
            y1 = [d["throughput_req_s"] for d in core1_data]
            plt.plot(x1, y1, marker='o', linewidth=2.5, markersize=8, color='#1f77b4', label='Core = 1 (150 paquetes)')
            for px, py in zip(x1, y1):
                plt.annotate(f"{py:.0f}", (px, py), textcoords="offset points", xytext=(0, 10), ha='center', fontweight='bold', color='#1f77b4')

        if core2_data:
            x2 = [d["hilos"] for d in core2_data]
            y2 = [d["throughput_req_s"] for d in core2_data]
            plt.plot(x2, y2, marker='s', linewidth=2.5, markersize=8, color='#ff7f0e', label='Core = 2 (150/300 paquetes)')
            for px, py in zip(x2, y2):
                plt.annotate(f"{py:.0f}", (px, py), textcoords="offset points", xytext=(0, -18), ha='center', fontweight='bold', color='#ff7f0e')

        plt.title('Rendimiento del Servidor Productor-Consumidor (Throughput)', fontsize=14, fontweight='bold', pad=15)
        plt.xlabel('Cantidad de Hilos (Workers del Thread Pool)', fontsize=12, labelpad=10)
        plt.ylabel('Throughput (peticiones/segundo)', fontsize=12, labelpad=10)
        plt.xticks([1, 2, 4, 8])
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.legend(frameon=True, facecolor='white', framealpha=0.9)
        chart1_path = os.path.join(RESULTS_DIR, "throughput_comparison.png")
        plt.savefig(chart1_path, dpi=200)
        plt.close()
        print(f"Gráfico guardado: {chart1_path}")

        plt.figure(figsize=(9, 5.5))
        if core1_data:
            x1 = [d["hilos"] for d in core1_data]
            y1 = [d["tiempo_ms"] for d in core1_data]
            plt.plot(x1, y1, marker='o', linewidth=2.5, markersize=8, color='#2ca02c', label='Core = 1 (150 paquetes)')
            for px, py in zip(x1, y1):
                plt.annotate(f"{py:.1f}ms", (px, py), textcoords="offset points", xytext=(0, 10), ha='center', fontweight='bold', color='#2ca02c')

        if core2_data:
            x2 = [d["hilos"] for d in core2_data]
            y2 = [d["tiempo_ms"] for d in core2_data]
            plt.plot(x2, y2, marker='^', linewidth=2.5, markersize=8, color='#d62728', label='Core = 2 (150/300 paquetes)')
            for px, py in zip(x2, y2):
                plt.annotate(f"{py:.1f}ms", (px, py), textcoords="offset points", xytext=(0, 10), ha='center', fontweight='bold', color='#d62728')

        plt.title('Tiempo Total de Procesamiento de Solicitudes', fontsize=14, fontweight='bold', pad=15)
        plt.xlabel('Cantidad de Hilos (Workers del Thread Pool)', fontsize=12, labelpad=10)
        plt.ylabel('Tiempo Total (milisegundos)', fontsize=12, labelpad=10)
        plt.xticks([1, 2, 4, 8])
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.legend(frameon=True, facecolor='white', framealpha=0.9)
        chart2_path = os.path.join(RESULTS_DIR, "execution_time.png")
        plt.savefig(chart2_path, dpi=200)
        plt.close()
        print(f"Gráfico guardado: {chart2_path}")

        fig, ax = plt.subplots(figsize=(11, 4.5))
        ax.axis('tight')
        ax.axis('off')

        headers = ["Cantidad de Cores", "Cantidad de hilos", "Cantidad de paquetes", "Tiempo Promedio", "Throughput", "Latencia Prom."]
        table_rows = []
        for d in data:
            table_rows.append([
                str(d["cores"]),
                str(d["hilos"]),
                str(d["paquetes"]),
                f"{d['tiempo_ms']:.2f} ms",
                f"{d['throughput_req_s']:.1f} req/s",
                f"{d['latencia_promedio_ms']:.4f} ms"
            ])

        table = ax.table(
            cellText=table_rows,
            colLabels=headers,
            cellLoc='center',
            loc='center'
        )
        table.auto_set_font_size(False)
        table.set_fontsize(11)
        table.scale(1.2, 1.8)

        for (row, col), cell in table.get_celld().items():
            if row == 0:
                cell.set_facecolor('#2b5c8f')
                cell.set_text_props(color='white', weight='bold')
            elif row % 2 == 0:
                cell.set_facecolor('#f3f6fa')

        chart3_path = os.path.join(RESULTS_DIR, "table_summary.png")
        plt.savefig(chart3_path, dpi=200, bbox_inches='tight')
        plt.close()
        print(f"Imagen de tabla guardada: {chart3_path}")

    except Exception as e:
        print(f"Error generando gráficos: {e}")

if __name__ == "__main__":
    main()
