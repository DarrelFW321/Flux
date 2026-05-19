import { DOC_SECTIONS } from '../data/docsSections';

interface DocsPaneProps {
  activeDocId: string;
}

export function DocsPane({ activeDocId }: DocsPaneProps) {
  const section = DOC_SECTIONS.find(s => s.id === activeDocId) ?? DOC_SECTIONS[0];

  return (
    <article className="docs-pane">
      <h2 className="docs-title">{section.title}</h2>
      <div className="docs-body">
        {section.body.split('\n\n').map((para, i) => (
          <p key={i}>{para}</p>
        ))}
      </div>
      <p className="docs-footer dim">
        Full reference in the repo: <code>docs/LANGUAGE.md</code> and{' '}
        <code>docs/BUILD.md</code>
      </p>
    </article>
  );
}
