#!/usr/bin/env python

import sys
import argparse
import os
import ntpath
from struct import *
import struct
from array import array
import pandas as pd 
import numpy as np
from tqdm import tqdm
from os import listdir
from pathlib import Path
from matplotlib import pyplot as plt


COL_NAME = 'latency_ms'


def show_stats(df, file_name, extension):
    stats_min = df[COL_NAME].min(); 
    stats_mean = df[COL_NAME].mean(); 
    stats_median = df[COL_NAME].median(); 
    stats_p80 = df[COL_NAME].quantile(0.8); 
    stats_p90 = df[COL_NAME].quantile(0.9); 
    stats_p95 = df[COL_NAME].quantile(0.95); 
    stats_p98 = df[COL_NAME].quantile(0.98); 
    stats_p99 = df[COL_NAME].quantile(0.99); 
    stats_max = df[COL_NAME].max(); 
    
    print("\n=========================")
    print("File = " + str(file_name) + str(extension))
    print("   Mean : " + "{:.2f}".format(stats_mean) + " ms")
    print("   Min : " + "{:.2f}".format(stats_min) + " ms")
    print("   P50 : " + "{:.2f}".format(stats_median) + " ms")
    print("   P80 : " + "{:.2f}".format(stats_p80) + " ms")
    print("   P90 : " + "{:.2f}".format(stats_p90) + " ms")
    print("   P95 : " + "{:.2f}".format(stats_p95) + " ms")
    print("   P98 : " + "{:.2f}".format(stats_p98) + " ms")
    print("   P99 : " + "{:.2f}".format(stats_p99) + " ms")
    print("   Max : " + "{:.2f}".format(stats_max) + " ms")

    return stats_mean, stats_max, stats_median, stats_p98

def generate_graph(df, out_dir, file_name, stats_mean, stats_max, stats_median, stats_p98):
    out_path = str(out_dir) + "/" + str(file_name) + ".png"
    fig = plt.figure()
    
    df['y_val'] = df.index / df.shape[0]
    plt.plot(df[COL_NAME], df['y_val'])
    
    x1,x2,y1,y2 = plt.axis()  
    plt.axis((0,min(5*stats_median,stats_max) ,0,1))
    
    fig.suptitle('Latency CDF of ' + file_name + ".txt")
    plt.xlabel('Latency (ms)')
    plt.ylabel('Percentile')

    plt.figtext(.6, .55,  "Mean = " + "{:.2f}".format(stats_mean) + " ms")
    plt.figtext(.6, .5,  "Median = " + "{:.2f}".format(stats_median) + " ms")
    plt.figtext(.6, .45, "P98  = " + "{:.2f}".format(stats_p98) + " ms")
    plt.figtext(.6, .4, "Max  = " + "{:.2f}".format(stats_max) + " ms")
    plt.savefig(out_path)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("-in_trace", help="the location of the trace file",type=str)

    args = parser.parse_args()
    if (not args.in_trace):
        print("ERROR: You must provide at least 1 argument: -in_trace <the latency trace file> ")
        exit(-1)
    else:
        # read trace file
        df = pd.read_csv (args.in_trace)
        df.columns=df.columns.str.strip()
        df = df.sort_values(by=COL_NAME, ignore_index=True)
        
        out_dir = Path(args.in_trace).parent
        file_name = ntpath.basename(os.path.splitext(args.in_trace)[0])
        extension = ntpath.basename(os.path.splitext(args.in_trace)[1])

        stats_mean, stats_max, stats_median, stats_p98 = show_stats(df, file_name, extension)
        
        generate_graph(df, out_dir, file_name, stats_mean, stats_max, stats_median, stats_p98)

        
    
