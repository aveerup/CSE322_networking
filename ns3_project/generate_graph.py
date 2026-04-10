#!venv/bin/python3

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


for algorithm in ["red", "rio", "ario", "ario-initial", "red-wireless", "rio-wireless", "ario-wireless"]:

    df = pd.read_csv(f"{algorithm}_out.csv")

    cases = df['case'].astype(int).drop_duplicates().values

    for case in cases:
        df1 = df[df['case'].astype(int)==case]

        x = df1['assured_rate'].astype(float).values
        y = df1['avgQ'].astype(float).values

        plt.figure(1)
        plt.plot(x, y, marker='o', label=f"case {case}")

        if algorithm == "red" or algorithm == "red-wireless":
           continue

        marked = df1['marked_green'].astype(float).values
        dropped = df1['dropped_green'].astype(float).values

        y1 = np.divide(dropped, marked, out=np.zeros_like(dropped), where=marked!=0)

        plt.figure(2)
        plt.plot(x, y1, marker='o', label=f"case {case}")

    plt.figure(1)
    plt.title(f"Average queue size in {algorithm}")
    plt.xlabel("assured_rate")
    plt.ylabel("avgQ")
    plt.legend()
    plt.grid(True)
    plt.savefig(f"avgQ_vs_assured_rate_in_{algorithm}.png")
    plt.clf()

    if algorithm == "red" or algorithm == "red-wireless":
        continue

    plt.figure(2)
    plt.title(f"Dropped green pkts in {algorithm}")
    plt.xlabel("assured_rate")
    plt.ylabel("dropped_green_ratio")
    plt.legend()
    plt.grid(True)
    plt.savefig(f"dropped_green_vs_assured_rate_in_{algorithm}.png")
    plt.clf()

