"""
Parse UART output from asl-overfit.bin or asl-overfit20.bin and plot:
  1. Loss curve (and accuracy if epoch mode)
  2. Gradient norms at step 0 (bar chart)

Usage:
    python plot_training.py pi_loss.txt      # per-step mode (STEP lines)
    python plot_training.py pi_loss20.txt    # per-epoch mode (EPOCH lines)
    python plot_training.py                  # reads /tmp/pi_loss.txt
"""

import sys, re, math
import matplotlib
matplotlib.use('Agg')   # no display needed — saves to PNG
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

def parse(path):
    # per-step mode: STEP 42 loss=2.7834 pred=E true=E CORRECT
    steps, losses, correct = [], [], []
    # per-epoch mode: EPOCH 5 loss=2.1234 acc=7/20
    epochs, epoch_losses, epoch_acc, epoch_total = [], [], [], []
    norms = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            m = re.match(r'STEP (\d+) loss=([\d.]+) pred=(\w) true=(\w) (\w+)', line)
            if m:
                steps.append(int(m.group(1)))
                losses.append(float(m.group(2)))
                correct.append(m.group(5) == 'CORRECT')
                continue
            m = re.match(r'EPOCH (\d+) loss=([\d.]+) acc=(\d+)/(\d+)', line)
            if m:
                epochs.append(int(m.group(1)))
                epoch_losses.append(float(m.group(2)))
                epoch_acc.append(int(m.group(3)))
                epoch_total.append(int(m.group(4)))
                continue
            m = re.match(r'NORM (\S+) ([\d.]+)', line)
            if m:
                norms[m.group(1)] = float(m.group(2))
    return steps, losses, correct, epochs, epoch_losses, epoch_acc, epoch_total, norms

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/pi_loss.txt'
    steps, losses, correct, epochs, epoch_losses, epoch_acc, epoch_total, norms = parse(path)

    mode = 'epoch' if epochs else 'step'

    if mode == 'step' and not steps:
        print(f"No STEP or EPOCH lines found in {path}")
        return

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle('TinyCNN On-Device Training — Bare-Metal Pi', fontsize=13, fontweight='bold')

    if mode == 'step':
        # --- Loss curve (per step) ---
        ax = axes[0]
        colors = ['#e74c3c' if not c else '#2ecc71' for c in correct]
        ax.scatter(steps, losses, c=colors, s=12, zorder=3)
        ax.plot(steps, losses, color='#3498db', linewidth=1.2, alpha=0.7, zorder=2)

        first_correct = next((s for s, c in zip(steps, correct) if c), None)
        if first_correct is not None:
            ax.axvline(first_correct, color='#2ecc71', linestyle='--', linewidth=1.5,
                       label=f'First correct (step {first_correct})')

        ax.axhline(math.log(24), color='gray', linestyle=':', linewidth=1,
                   label=f'Random baseline ln(24)={math.log(24):.2f}')
        ax.axhline(0.05, color='orange', linestyle=':', linewidth=1,
                   label='Overfit threshold 0.05')

        init_loss  = losses[0]
        final_loss = losses[-1]
        ax.set_xlabel('SGD Step')
        ax.set_ylabel('Cross-Entropy Loss')
        ax.set_title(f'Loss: {init_loss:.4f} → {final_loss:.4f} ({len(steps)} steps, lr=0.01)')
        ax.set_ylim(bottom=0)
        ax.grid(True, alpha=0.3)

        wrong_patch   = mpatches.Patch(color='#e74c3c', label='Wrong prediction')
        correct_patch = mpatches.Patch(color='#2ecc71', label='Correct prediction')
        ax.legend(handles=[wrong_patch, correct_patch] + ax.get_legend_handles_labels()[0][1:],
                  fontsize=7)

        print(f"\nSummary:")
        print(f"  Steps:         {len(steps)}")
        print(f"  Init loss:     {init_loss:.4f}  (random baseline = {math.log(24):.4f})")
        print(f"  Final loss:    {final_loss:.4f}")
        print(f"  First correct: step {first_correct}")
        n_correct = sum(correct)
        print(f"  Correct steps: {n_correct}/{len(steps)} = {100*n_correct/len(steps):.0f}%")

    else:
        # --- Loss + accuracy curves (per epoch) ---
        ax = axes[0]
        n = epoch_total[0] if epoch_total else 20
        acc_frac = [a / n for a in epoch_acc]

        ax2_twin = ax.twinx()
        ax.plot(epochs, epoch_losses, color='#3498db', linewidth=1.5, label='Loss')
        ax2_twin.plot(epochs, acc_frac, color='#2ecc71', linewidth=1.5, linestyle='--',
                      label=f'Accuracy (/{n})')

        ax.axhline(math.log(24), color='gray', linestyle=':', linewidth=1,
                   label=f'Random ln(24)={math.log(24):.2f}')
        ax.axhline(0.05, color='orange', linestyle=':', linewidth=1, label='0.05 threshold')

        init_loss  = epoch_losses[0]
        final_loss = epoch_losses[-1]
        final_acc  = epoch_acc[-1]

        ax.set_xlabel('Epoch')
        ax.set_ylabel('Cross-Entropy Loss', color='#3498db')
        ax2_twin.set_ylabel('Accuracy', color='#2ecc71')
        ax2_twin.set_ylim(0, 1)
        ax.set_ylim(bottom=0)
        ax.set_title(f'20-sample overfit: loss {init_loss:.4f}→{final_loss:.4f}, '
                     f'acc {final_acc}/{n} ({len(epochs)} epochs)')
        ax.grid(True, alpha=0.3)

        lines1, labels1 = ax.get_legend_handles_labels()
        lines2, labels2 = ax2_twin.get_legend_handles_labels()
        ax.legend(lines1 + lines2, labels1 + labels2, fontsize=7)

        print(f"\nSummary:")
        print(f"  Epochs:       {len(epochs)}")
        print(f"  Samples:      {n}")
        print(f"  Init loss:    {init_loss:.4f}  (random baseline = {math.log(24):.4f})")
        print(f"  Final loss:   {final_loss:.4f}")
        print(f"  Final acc:    {final_acc}/{n} = {100*final_acc/n:.0f}%")
        # epoch where accuracy first hits 100%
        perfect = next((e for e, a in zip(epochs, epoch_acc) if a == n), None)
        if perfect is not None:
            print(f"  100% acc at:  epoch {perfect}")

    if norms:
        dead = [name for name, v in norms.items() if v < 1e-3]
        print(f"  Dead layers:  {dead if dead else 'none'}")

    # --- Right: gradient norms ---
    ax_n = axes[1]
    if norms:
        names = list(norms.keys())
        vals  = [norms[n] for n in names]
        bar_colors = ['#e74c3c' if v < 1e-3 else '#3498db' for v in vals]
        bars = ax_n.bar(range(len(names)), vals, color=bar_colors)
        ax_n.set_xticks(range(len(names)))
        ax_n.set_xticklabels(names, rotation=45, ha='right', fontsize=8)
        ax_n.set_ylabel('L2 Norm of Gradient')
        ax_n.set_title('Gradient Norms at Step 0\n(red = dead/zero, blue = flowing)')
        ax_n.grid(True, alpha=0.3, axis='y')
        for bar, v in zip(bars, vals):
            ax_n.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.001,
                      f'{v:.4f}', ha='center', va='bottom', fontsize=7)
    else:
        ax_n.text(0.5, 0.5, 'No NORM lines found', ha='center', va='center',
                  transform=ax_n.transAxes)

    plt.tight_layout()
    out = 'pi_training_plot.png'
    plt.savefig(out, dpi=150, bbox_inches='tight')
    print(f"Saved: {out}")

if __name__ == '__main__':
    main()
