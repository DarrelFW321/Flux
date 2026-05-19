interface WelcomeBannerProps {
  onDismiss: () => void;
  onGoPlayground?: () => void;
  onGoBenchmarks?: () => void;
}

export function WelcomeBanner({ onDismiss, onGoPlayground, onGoBenchmarks }: WelcomeBannerProps) {
  return (
    <section className="welcome-banner" aria-label="About Flux">
      <div className="welcome-content">
        <h1 className="welcome-title">Flux compiler visualizer</h1>
        <p className="welcome-lead">
          Flux is a small compiled language for <strong>numerical kernels</strong> — fixed-size
          arrays, loops, and builtins like <code>dot</code> and <code>sum</code>. This site has
          two areas: experiment with your own code, or run fixed performance benchmarks.
        </p>
        <ul className="welcome-steps">
          <li>
            <span className="welcome-step-label">Playground</span>
            Edit examples in the editor. The <strong>Pipeline</strong> tab (tokens → AST → MIR)
            updates live in your browser; <strong>Run</strong> compiles and executes that same
            code on the server.
          </li>
          <li>
            <span className="welcome-step-label">Benchmarks</span>
            Separate page with <strong>predetermined kernels</strong> from the repo (
            <code>benchmarks/</code>) — not your editor text. Compare Flux, C, and NumPy on
            identical workloads.
          </li>
        </ul>
        {(onGoPlayground || onGoBenchmarks) && (
          <div className="welcome-actions">
            {onGoPlayground && (
              <button type="button" className="welcome-action-btn" onClick={onGoPlayground}>
                Open Playground
              </button>
            )}
            {onGoBenchmarks && (
              <button type="button" className="welcome-action-btn secondary" onClick={onGoBenchmarks}>
                View Benchmarks
              </button>
            )}
          </div>
        )}
      </div>
      <button type="button" className="welcome-dismiss" onClick={onDismiss} aria-label="Dismiss intro">
        Dismiss
      </button>
    </section>
  );
}
