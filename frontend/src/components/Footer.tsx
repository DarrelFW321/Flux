const LINKEDIN_URL = 'https://www.linkedin.com/in/darrel-wihandi/';
const GITHUB_URL = 'https://github.com/DarrelFW321/flux';

export function Footer() {
  return (
    <footer className="site-footer">
      <span className="site-footer-credit">
        <span className="site-footer-name">Darrel Wihandi</span>
        <span className="site-footer-sep" aria-hidden>·</span>
        <span className="site-footer-role">SE @ UW</span>
      </span>
      <nav className="site-footer-links" aria-label="Author links">
        <a
          href={LINKEDIN_URL}
          target="_blank"
          rel="noopener noreferrer"
          className="site-footer-link"
        >
          LinkedIn
        </a>
        <a
          href={GITHUB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className="site-footer-link"
        >
          GitHub
        </a>
      </nav>
    </footer>
  );
}
