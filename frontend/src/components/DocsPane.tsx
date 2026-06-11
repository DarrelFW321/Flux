import { useEffect, useRef } from 'react';
import { DOC_SECTIONS } from '../data/docsSections';

interface DocsPaneProps {
  activeDocId: string;
}

type Block =
  | { kind: 'code'; text: string }
  | { kind: 'p'; text: string };

// Minimal markdown: ``` fences become code blocks, blank lines split paragraphs.
function parseBlocks(body: string): Block[] {
  const blocks: Block[] = [];
  body.split('```').forEach((chunk, i) => {
    const text = chunk.replace(/^\n+|\n+$/g, '');
    if (!text) return;
    if (i % 2 === 1) {
      blocks.push({ kind: 'code', text });
    } else {
      text.split(/\n{2,}/).forEach(p => {
        const para = p.trim();
        if (para) blocks.push({ kind: 'p', text: para });
      });
    }
  });
  return blocks;
}

// `backticks` inside a paragraph become inline <code>.
function renderInline(text: string) {
  return text.split('`').map((seg, i) =>
    i % 2 === 1 ? <code key={i}>{seg}</code> : <span key={i}>{seg}</span>,
  );
}

export function DocsPane({ activeDocId }: DocsPaneProps) {
  const containerRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const el = containerRef.current?.querySelector(`#doc-${activeDocId}`);
    el?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }, [activeDocId]);

  return (
    <article className="docs-pane" ref={containerRef}>
      {DOC_SECTIONS.map(section => (
        <section key={section.id} id={`doc-${section.id}`} className="docs-section">
          <h2 className="docs-title">{section.title}</h2>
          <div className="docs-body">
            {parseBlocks(section.body).map((block, i) =>
              block.kind === 'code'
                ? <pre key={i} className="docs-code"><code>{block.text}</code></pre>
                : <p key={i}>{renderInline(block.text)}</p>,
            )}
          </div>
        </section>
      ))}
      <p className="docs-footer dim">
        Full reference in the repo: <code>docs/LANGUAGE.md</code> and{' '}
        <code>docs/BUILD.md</code>
      </p>
    </article>
  );
}
