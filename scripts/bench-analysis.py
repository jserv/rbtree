#!/usr/bin/env python3

"""
Advanced Red-Black Tree Benchmark Analysis
Provides detailed performance analysis and comparison
"""

import sys
import json
import statistics
import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
from collections import defaultdict

class BenchmarkAnalyzer:
    def __init__(self, xml_file):
        self.xml_file = xml_file
        self.root = self._parse_xml()
        self.data = self._extract_all_data()
    
    def _parse_xml(self):
        """Parse XML benchmark results"""
        try:
            tree = ET.parse(self.xml_file)
            return tree.getroot()
        except ET.ParseError as e:
            print(f"Error parsing XML file: {e}")
            sys.exit(1)
        except FileNotFoundError:
            print(f"File not found: {self.xml_file}")
            sys.exit(1)
    
    def _extract_all_data(self):
        """Extract all benchmark data"""
        data = {}
        test_types = ['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear']
        
        for test_type in test_types:
            data[test_type] = self._extract_test_data(test_type)
        
        return data
    
    def _extract_test_data(self, test_type):
        """Extract benchmark data for a specific test type"""
        implementations = {}
        
        for rb_test in self.root.findall('RBTest'):
            impl = rb_test.get('implementation')
            node_size = int(rb_test.get('nodeSize', 0))
            
            test_section = rb_test.find(test_type)
            if test_section is None:
                continue
            
            samples = []
            for sample in test_section.findall('Sample'):
                node_count = int(sample.get('nodeCount'))
                duration = int(sample.get('duration'))
                insert_count = int(sample.get('insertCount', 0))
                extract_count = int(sample.get('extractCount', 0))
                
                total_ops = insert_count + extract_count
                if total_ops > 0 and duration > 0:
                    ops_per_sec = (total_ops * 1e9) / duration
                    ns_per_op = duration / total_ops
                else:
                    ops_per_sec = 0
                    ns_per_op = float('inf')
                
                samples.append({
                    'node_count': node_count,
                    'duration_ns': duration,
                    'insert_count': insert_count,
                    'extract_count': extract_count,
                    'total_ops': total_ops,
                    'ops_per_sec': ops_per_sec,
                    'ns_per_op': ns_per_op,
                    'duration_sec': duration / 1e9
                })
            
            if samples:
                implementations[impl] = {
                    'samples': samples,
                    'node_size': node_size
                }
        
        return implementations
    
    def calculate_statistics(self, test_type=None):
        """Calculate performance statistics"""
        if test_type:
            test_data = {test_type: self.data[test_type]}
        else:
            test_data = self.data
        
        stats = {}
        
        for test_name, implementations in test_data.items():
            stats[test_name] = {}
            
            for impl, impl_data in implementations.items():
                samples = impl_data['samples']
                if not samples:
                    continue
                
                ops_per_sec = [s['ops_per_sec'] for s in samples if s['ops_per_sec'] > 0]
                ns_per_op = [s['ns_per_op'] for s in samples if s['ns_per_op'] != float('inf')]
                node_counts = [s['node_count'] for s in samples]
                
                if ops_per_sec:
                    stats[test_name][impl] = {
                        'node_size': impl_data['node_size'],
                        'sample_count': len(samples),
                        'node_count_range': (min(node_counts), max(node_counts)),
                        'ops_per_sec': {
                            'mean': statistics.mean(ops_per_sec),
                            'median': statistics.median(ops_per_sec),
                            'stdev': statistics.stdev(ops_per_sec) if len(ops_per_sec) > 1 else 0,
                            'min': min(ops_per_sec),
                            'max': max(ops_per_sec)
                        },
                        'ns_per_op': {
                            'mean': statistics.mean(ns_per_op),
                            'median': statistics.median(ns_per_op),
                            'stdev': statistics.stdev(ns_per_op) if len(ns_per_op) > 1 else 0,
                            'min': min(ns_per_op),
                            'max': max(ns_per_op)
                        }
                    }
        
        return stats
    
    def generate_detailed_report(self, output_file=None):
        """Generate comprehensive text report"""
        stats = self.calculate_statistics()
        
        report = []
        report.append("=" * 60)
        report.append("RED-BLACK TREE COMPREHENSIVE BENCHMARK REPORT")
        report.append("=" * 60)
        report.append("")
        
        platform = self.root.get('platform', 'Unknown')
        compiler = self.root.get('compiler', 'Unknown')
        report.append(f"Platform: {platform}")
        report.append(f"Compiler: {compiler}")
        report.append("")
        
        for test_name, test_stats in stats.items():
            report.append(f"{'=' * 20} {test_name} {'=' * 20}")
            report.append("")
            
            if not test_stats:
                report.append("No data available for this test.")
                report.append("")
                continue
            
            # Find best performing implementation
            best_impl = None
            best_ops = 0
            for impl, impl_stats in test_stats.items():
                if impl_stats['ops_per_sec']['mean'] > best_ops:
                    best_ops = impl_stats['ops_per_sec']['mean']
                    best_impl = impl
            
            for impl, impl_stats in test_stats.items():
                report.append(f"Implementation: {impl}")
                report.append(f"  Node size: {impl_stats['node_size']} bytes")
                report.append(f"  Samples: {impl_stats['sample_count']}")
                report.append(f"  Node count range: {impl_stats['node_count_range'][0]:,} - {impl_stats['node_count_range'][1]:,}")
                report.append("")
                
                ops_stats = impl_stats['ops_per_sec']
                ns_stats = impl_stats['ns_per_op']
                
                report.append("  Operations per second:")
                report.append(f"    Mean: {ops_stats['mean']:,.0f}")
                report.append(f"    Median: {ops_stats['median']:,.0f}")
                report.append(f"    Std Dev: {ops_stats['stdev']:,.0f}")
                report.append(f"    Range: {ops_stats['min']:,.0f} - {ops_stats['max']:,.0f}")
                
                if impl == best_impl:
                    report.append("    ★ BEST PERFORMANCE ★")
                else:
                    relative_perf = (ops_stats['mean'] / best_ops) * 100
                    report.append(f"    Relative to best: {relative_perf:.1f}%")
                
                report.append("")
                
                report.append("  Nanoseconds per operation:")
                report.append(f"    Mean: {ns_stats['mean']:.1f} ns")
                report.append(f"    Median: {ns_stats['median']:.1f} ns")
                report.append(f"    Std Dev: {ns_stats['stdev']:.1f} ns")
                report.append(f"    Range: {ns_stats['min']:.1f} - {ns_stats['max']:.1f} ns")
                report.append("")
                
                report.append("-" * 40)
                report.append("")
        
        # Add memory efficiency analysis
        report.append("=" * 20 + " MEMORY EFFICIENCY " + "=" * 20)
        report.append("")
        
        for test_name, test_stats in stats.items():
            if not test_stats:
                continue
            
            report.append(f"{test_name}:")
            for impl, impl_stats in test_stats.items():
                node_size = impl_stats['node_size']
                ops_per_sec = impl_stats['ops_per_sec']['mean']
                efficiency = ops_per_sec / node_size if node_size > 0 else 0
                report.append(f"  {impl}: {efficiency:,.0f} ops/sec/byte")
            report.append("")
        
        report_text = "\n".join(report)
        
        if output_file:
            with open(output_file, 'w') as f:
                f.write(report_text)
            print(f"Detailed report saved to {output_file}")
        else:
            print(report_text)
        
        return report_text
    
    def plot_scalability_analysis(self, output_file=None):
        """Create scalability analysis plots"""
        fig, axes = plt.subplots(2, 2, figsize=(16, 12))
        
        test_types = ['SmallSetRandomOps', 'LargeSetRandomOps', 'SmallSetLinear', 'LargeSetLinear']
        colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b']
        
        for idx, test_type in enumerate(test_types):
            if idx >= len(axes.flatten()):
                break
            
            ax = axes.flatten()[idx]
            implementations = self.data[test_type]
            color_idx = 0
            
            for impl, impl_data in implementations.items():
                samples = impl_data['samples']
                if not samples:
                    continue
                
                x = [s['node_count'] for s in samples]
                y = [s['ops_per_sec'] for s in samples if s['ops_per_sec'] > 0]
                x_filtered = [s['node_count'] for s in samples if s['ops_per_sec'] > 0]
                
                if x_filtered and y:
                    color = colors[color_idx % len(colors)]
                    color_idx += 1
                    
                    # Plot data points
                    ax.loglog(x_filtered, y, 'o-', color=color, label=impl, 
                             markersize=4, linewidth=2, alpha=0.8)
                    
                    # Add trend line for large datasets
                    if len(x_filtered) > 5:
                        # Fit logarithmic trend
                        log_x = np.log(x_filtered)
                        log_y = np.log(y)
                        if len(log_x) > 1:
                            coeffs = np.polyfit(log_x, log_y, 1)
                            trend_x = np.logspace(np.log10(min(x_filtered)), 
                                                np.log10(max(x_filtered)), 100)
                            trend_y = np.exp(coeffs[1]) * (trend_x ** coeffs[0])
                            ax.loglog(trend_x, trend_y, '--', color=color, alpha=0.5)
            
            ax.set_xlabel('Node Count')
            ax.set_ylabel('Operations/sec')
            ax.set_title(f'{test_type}')
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=8)
            
            # Add theoretical O(log n) reference line
            if implementations:
                sample_impl = next(iter(implementations.values()))
                sample_x = [s['node_count'] for s in sample_impl['samples']]
                if sample_x:
                    ref_x = np.logspace(np.log10(min(sample_x)), np.log10(max(sample_x)), 100)
                    # Normalize to show O(log n) behavior
                    ref_y = 1e6 / np.log2(ref_x)  # Arbitrary scaling
                    ax.loglog(ref_x, ref_y, 'k:', alpha=0.5, label='O(1/log n) reference')
        
        plt.suptitle('Red-Black Tree Scalability Analysis', fontsize=16)
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"Scalability analysis saved to {output_file}")
        else:
            plt.show()
    
    def export_json_data(self, output_file):
        """Export benchmark data as JSON for further analysis"""
        # Convert data to JSON-serializable format
        json_data = {
            'metadata': {
                'platform': self.root.get('platform', 'Unknown'),
                'compiler': self.root.get('compiler', 'Unknown'),
                'xml_file': str(self.xml_file)
            },
            'statistics': self.calculate_statistics(),
            'raw_data': {}
        }
        
        # Add raw data
        for test_type, implementations in self.data.items():
            json_data['raw_data'][test_type] = {}
            for impl, impl_data in implementations.items():
                json_data['raw_data'][test_type][impl] = {
                    'node_size': impl_data['node_size'],
                    'samples': impl_data['samples']
                }
        
        with open(output_file, 'w') as f:
            json.dump(json_data, f, indent=2)
        
        print(f"JSON data exported to {output_file}")

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Advanced red-black tree benchmark analysis')
    parser.add_argument('xml_file', help='XML benchmark results file')
    parser.add_argument('--report', help='Generate detailed text report')
    parser.add_argument('--scalability', help='Generate scalability analysis plot')
    parser.add_argument('--json', help='Export data as JSON')
    parser.add_argument('--all', help='Generate all outputs with this base name')
    
    args = parser.parse_args()
    
    analyzer = BenchmarkAnalyzer(args.xml_file)
    
    if args.all:
        base_name = args.all
        analyzer.generate_detailed_report(f"{base_name}_detailed_report.txt")
        analyzer.plot_scalability_analysis(f"{base_name}_scalability.png")
        analyzer.export_json_data(f"{base_name}_data.json")
    else:
        if args.report:
            analyzer.generate_detailed_report(args.report)
        
        if args.scalability:
            analyzer.plot_scalability_analysis(args.scalability)
        
        if args.json:
            analyzer.export_json_data(args.json)
        
        if not any([args.report, args.scalability, args.json]):
            # Default: show basic report
            analyzer.generate_detailed_report()

if __name__ == '__main__':
    main()