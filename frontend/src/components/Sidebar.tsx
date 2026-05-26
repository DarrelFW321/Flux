import { EXAMPLES, type FluxExample } from '../data/examples';
import { DOC_SECTIONS } from '../data/docsSections';

export type SidebarView = 'examples' | 'docs';

interface SidebarProps {
  view: SidebarView;
  onViewChange: (v: SidebarView) => void;
  selectedExampleId: string;
  onSelectExample: (ex: FluxExample) => void;
  activeDocId: string;
  onSelectDoc: (id: string) => void;
  collapsed: boolean;
  onToggleCollapse: () => void;
}

export function Sidebar({
  view,
  onViewChange,
  selectedExampleId,
  onSelectExample,
  activeDocId,
  onSelectDoc,
  collapsed,
  onToggleCollapse,
}: SidebarProps) {
  if (collapsed) {
    return (
      <aside className="sidebar sidebar--collapsed">
        <button
          type="button"
          className="sidebar-collapse-btn"
          onClick={onToggleCollapse}
          title="Show panel"
          aria-label="Show panel"
        >
          ›
        </button>
      </aside>
    );
  }

  return (
    <aside className="sidebar">
      <div className="sidebar-switch">
        <button
          type="button"
          className={`sidebar-switch-btn ${view === 'examples' ? 'active' : ''}`}
          onClick={() => onViewChange('examples')}
        >
          Examples
        </button>
        <button
          type="button"
          className={`sidebar-switch-btn ${view === 'docs' ? 'active' : ''}`}
          onClick={() => onViewChange('docs')}
        >
          Docs
        </button>
        <button
          type="button"
          className="sidebar-collapse-btn sidebar-collapse-btn--inline"
          onClick={onToggleCollapse}
          title="Hide panel"
          aria-label="Hide panel"
        >
          ‹
        </button>
      </div>

      <div className="sidebar-body">
        {view === 'examples' ? (
          <ul className="example-list">
            {EXAMPLES.map(ex => (
              <li key={ex.id}>
                <button
                  type="button"
                  className={`example-item ${ex.id === selectedExampleId ? 'active' : ''}`}
                  onClick={() => onSelectExample(ex)}
                >
                  <span className="example-title">{ex.title}</span>
                  <span className="example-desc">{ex.description}</span>
                  <span className="example-tags">
                    {ex.tags.map(t => (
                      <span key={t} className="example-tag">{t}</span>
                    ))}
                  </span>
                </button>
              </li>
            ))}
          </ul>
        ) : (
          <ul className="doc-list">
            {DOC_SECTIONS.map(sec => (
              <li key={sec.id}>
                <button
                  type="button"
                  className={`doc-item ${sec.id === activeDocId ? 'active' : ''}`}
                  onClick={() => onSelectDoc(sec.id)}
                >
                  {sec.title}
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>
    </aside>
  );
}
