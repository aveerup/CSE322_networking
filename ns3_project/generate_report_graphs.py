#!venv/bin/python3
import os
import pandas as pd
import matplotlib.pyplot as plt

def plot_graph(id, x, y, xlabel, ylabel, label, title):
    plt.figure(id)
    plt.plot(x, y, marker='o', label=label)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True)
    plt.legend()

def save_plot(id, folder, file):
    plt.figure(id)

    if not os.path.exists(f"{folder}"):
        os.makedirs(f"{folder}")

    plt.savefig(f"./{folder}/{file}.png")
    plt.close(id)


for algorithm in [ "rio-report", "rio-wireless-report", "ario-report", "ario-wireless-report" ]:
    df = pd.read_csv(f"{algorithm}_out.csv")

    df_nodes = df.iloc[0:5]
    df_flows = df.iloc[5:10]
    df_pps   = df.iloc[10:15]

    plot_graph(1, df_nodes["nSources"], df_nodes["throughput"], "nNodes", "Throughput", algorithm, "Throughput vs nNodes")
    plot_graph(2, df_nodes["nSources"], df_nodes["avg_delay"], "nNodes", "Delay", algorithm, "Delay vs nNodes")
    plot_graph(3, df_nodes["nSources"], df_nodes["delivery_ratio"], "nNodes", "Delivery Ratio", algorithm, "Delivery Ratio vs nNodes")
    plot_graph(4, df_nodes["nSources"], df_nodes["drop_ratio"], "nNodes", "Drop Ratio", algorithm, "Drop Ratio vs nNodes")


    plot_graph(5, df_flows["nFlows"], df_flows["throughput"], "nFlows", "Throughput", algorithm, "Throughput vs nFlows")
    plot_graph(6, df_flows["nFlows"], df_flows["avg_delay"], "nFlows", "Delay", algorithm, "Delay vs nFlows")
    plot_graph(7, df_flows["nFlows"], df_flows["delivery_ratio"], "nFlows", "Delivery Ratio", algorithm, "Delivery Ratio vs nFlows")
    plot_graph(8, df_flows["nFlows"], df_flows["drop_ratio"], "nFlows", "Drop Ratio", algorithm, "Drop Ratio vs nFlows")


    plot_graph(9, df_pps["pps"], df_pps["throughput"], "Packets/sec", "Throughput", algorithm, "Throughput vs PPS")
    plot_graph(10, df_pps["pps"], df_pps["avg_delay"], "Packets/sec", "Delay", algorithm, "Delay vs PPS")
    plot_graph(11, df_pps["pps"], df_pps["delivery_ratio"], "Packets/sec", "Delivery Ratio", algorithm, "Delivery Ratio vs PPS")
    plot_graph(12, df_pps["pps"], df_pps["drop_ratio"], "Packets/sec", "Drop Ratio", algorithm, "Drop Ratio vs PPS")


for i, xlabel in enumerate(["nNodes", "nFlows", "pps"]):
    for j, ylabel in enumerate(["throughput", "avg_delay", "delivery_ratio", "drop_ratio"]):
        save_plot(i*4 + j + 1, xlabel, f"{ylabel}_vs_{xlabel}")
