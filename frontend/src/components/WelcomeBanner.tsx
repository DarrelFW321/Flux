interface WelcomeBannerProps {
  onDismiss: () => void;
}

export function WelcomeBanner({ onDismiss }: WelcomeBannerProps) {
  return (
    <section className="welcome-banner" aria-label="About Flux">
      <div className="welcome-content">
        <h1 className="welcome-title">Flux compiler visualizer</h1>
        <p className="welcome-lead">
          Flux is a small compiled language for <strong>numerical kernels</strong> — fixed-size
          arrays, loops, and builtins like <code>dot</code> and <code>sum</code>. This site lets
          you watch the compiler work: from source code to native machine code.
        </p>
        <ul className="welcome-steps">
          <li>
            <span className="welcome-step-label">Edit</span>
            Pick an example or write Flux in the editor.
          </li>
          <li>
            <span className="welcome-step-label">Pipeline</span>
            Tokens, AST, and MIR update live in your browser (WASM).
          </li>
          <li>
            <span className="welcome-step-label">Run &amp; benchmark</span>
            LLVM IR, execution, and Flux vs C vs NumPy use the backend API.
          </li>
        </ul>
      </div>
      <button type="button" className="welcome-dismiss" onClick={onDismiss} aria-label="Dismiss intro">
        Dismiss
      </button>
    </section>
  );
}
