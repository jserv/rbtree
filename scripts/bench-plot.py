#!/usr/bin/env python3

"""
Red-Black Tree Benchmark Visualization Script
Adapted from rb-bench plot.py for local rbtree implementation
"""

import sys
import argparse
import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
from pathlib import Path

def parse_xml(xml_file):
    """Parse XML benchmark results"""
    try:
        tree = ET.parse(xml_file)
        root = tree.getroot()
        return root
    except ET.ParseError as e:
        print(f"Error parsing XML file: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"File not found: {xml_file}")
        sys.exit(1)

def extract_data(root, test_type):
    """Extract benchmark data for a specific test type"""
    data = {}
    
    for rb_test in root.findall('RBTest'):
        impl = rb_test.get('implementation')
        node_size = rb_test.get('nodeSize')
        
        test_section = rb_test.find(test_type)
        if test_section is None:
            continue
            
        samples = []
        for sample in test_section.findall('Sample'):
            node_count = int(sample.get('nodeCount'))
            duration = int(sample.get('duration'))
            insert_count = int(sample.get('insertCount', 0))
            extract_count = int(sample.get('extractCount', 0))
            
            # Convert to operations per second
            total_ops = insert_count + extract_count
            if total_ops > 0 and duration > 0:
                ops_per_sec = (total_ops * 1e9) / duration
            else:
                ops_per_sec = 0
                
            samples.append({
                'node_count': node_count,
                'duration_ns': duration,
                'ops_per_sec': ops_per_sec,
                'duration_sec': duration / 1e9
            })
        
        if samples:
            data[impl] = {
                'samples': samples,
                'node_size': node_size
            }
    
    return data

def plot_performance(data, test_type, output_file=None):
    """Create performance plots"""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b']
    color_idx = 0
    
    for impl, impl_data in data.items():
        samples = impl_data['samples']
        if not samples:
            continue
            
        x = [s['node_count'] for s in samples]
        y1 = [s['duration_sec'] for s in samples]
        y2 = [s['ops_per_sec'] for s in samples if s['ops_per_sec'] > 0]
        x2 = [s['node_count'] for s in samples if s['ops_per_sec'] > 0]
        
        color = colors[color_idx % len(colors)]
        color_idx += 1
        
        # Duration plot
        ax1.loglog(x, y1, 'o-', color=color, label=impl, markersize=4, linewidth=2)
        
        # Operations per second plot  
        if x2 and y2:
            ax2.semilogx(x2, y2, 'o-', color=color, label=impl, markersize=4, linewidth=2)
    
    # Format duration plot
    ax1.set_xlabel('Node Count')
    ax1.set_ylabel('Duration (seconds)')
    ax1.set_title(f'Red-Black Tree Benchmark: {test_type} - Duration')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Format operations per second plot
    ax2.set_xlabel('Node Count')
    ax2.set_ylabel('Operations per Second')
    ax2.set_title(f'Red-Black Tree Benchmark: {test_type} - Throughput')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Plot saved to {output_file}")
    else:
        plt.show()

def plot_comparison(data_dict, test_types, output_file=None):
    """Create comparison plots across different test types"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    axes = axes.flatten()
    
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b']
    
    for idx, test_type in enumerate(test_types):
        if idx >= len(axes):
            break
            
        ax = axes[idx]
        data = data_dict.get(test_type, {})
        color_idx = 0
        
        for impl, impl_data in data.items():
            samples = impl_data['samples']
            if not samples:
                continue
                
            x = [s['node_count'] for s in samples]
            y = [s['ops_per_sec'] for s in samples if s['ops_per_sec'] > 0]
            x_filtered = [s['node_count'] for s in samples if s['ops_per_sec'] > 0]
            
            if x_filtered and y:
                color = colors[color_idx % len(colors)]
                color_idx += 1
                ax.semilogx(x_filtered, y, 'o-', color=color, label=impl, markersize=3, linewidth=1.5)
        
        ax.set_xlabel('Node Count')
        ax.set_ylabel('Operations/sec')
        ax.set_title(f'{test_type}')
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
    
    plt.suptitle('Red-Black Tree Implementation Comparison', fontsize=16)
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Comparison plot saved to {output_file}")
    else:
        plt.show()

def generate_report(root, output_file=None):
    """Generate text report with benchmark statistics"""
    report = []
    report.append("=== Red-Black Tree Benchmark Report ===\n")
    
    platform = root.get('platform', 'Unknown')
    compiler = root.get('compiler', 'Unknown')
    report.append(f"Platform: {platform}")
    report.append(f"Compiler: {compiler}\n")
    
    test_types = ['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear']
    
    for test_type in test_types:
        report.append(f"--- {test_type} ---")
        data = extract_data(root, test_type)
        
        for impl, impl_data in data.items():
            samples = impl_data['samples']
            if not samples:
                continue
                
            node_counts = [s['node_count'] for s in samples]
            ops_per_sec = [s['ops_per_sec'] for s in samples if s['ops_per_sec'] > 0]
            
            if ops_per_sec:
                avg_ops = np.mean(ops_per_sec)
                max_ops = np.max(ops_per_sec)
                min_ops = np.min(ops_per_sec)
                
                report.append(f"  {impl}:")
                report.append(f"    Node size: {impl_data['node_size']} bytes")
                report.append(f"    Samples: {len(samples)}")
                report.append(f"    Node count range: {min(node_counts)} - {max(node_counts)}")
                report.append(f"    Throughput: avg={avg_ops:.0f}, max={max_ops:.0f}, min={min_ops:.0f} ops/sec")
        
        report.append("")
    
    report_text = "\n".join(report)
    
    if output_file:
        with open(output_file, 'w') as f:
            f.write(report_text)
        print(f"Report saved to {output_file}")
    else:
        print(report_text)
    
    return report_text

def main():
    parser = argparse.ArgumentParser(description='Visualize red-black tree benchmark results')
    parser.add_argument('xml_file', help='XML benchmark results file')
    parser.add_argument('--output', '-o', help='Output file for plots')
    parser.add_argument('--test-type', choices=['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear'],
                       help='Specific test type to plot')
    parser.add_argument('--comparison', action='store_true', help='Generate comparison plot across all test types')
    parser.add_argument('--report', help='Generate text report file')
    parser.add_argument('--format', choices=['png', 'pdf', 'svg'], default='png', help='Output format for plots')
    
    args = parser.parse_args()
    
    # Parse XML data
    root = parse_xml(args.xml_file)
    
    # Generate report if requested
    if args.report:
        generate_report(root, args.report)
    
    # Generate plots
    if args.comparison:
        # Generate comparison plot
        test_types = ['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear']
        data_dict = {}
        for test_type in test_types:
            data_dict[test_type] = extract_data(root, test_type)
        
        output_file = args.output
        if output_file and not output_file.endswith(f'.{args.format}'):
            output_file += f'.{args.format}'
            
        plot_comparison(data_dict, test_types, output_file)
        
    elif args.test_type:
        # Generate plot for specific test type
        data = extract_data(root, args.test_type)
        
        output_file = args.output
        if output_file and not output_file.endswith(f'.{args.format}'):
            output_file += f'.{args.format}'
            
        plot_performance(data, args.test_type, output_file)
        
    else:
        # Generate plots for all test types
        test_types = ['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear']
        
        for test_type in test_types:
            data = extract_data(root, test_type)
            if not data:
                continue
                
            if args.output:
                base_name = Path(args.output).stem
                output_file = f"{base_name}_{test_type}.{args.format}"
            else:
                output_file = None
                
            plot_performance(data, test_type, output_file)

if __name__ == '__main__':
    main()