export type PipelineTab = 'tokens' | 'ast' | 'mir' | 'ir';
export type InspectorTab = PipelineTab | 'output';

const PIPELINE_TABS: { id: PipelineTab; label: string }[] = [
  { id: 'tokens', label: 'Tokens' },
  { id: 'ast',    label: 'AST' },
  { id: 'mir',    label: 'MIR' },
  { id: 'ir',     label: 'IR' },
];

const MODES: { id: 'pipeline' | 'run'; label: string; tab: InspectorTab }[] = [
  { id: 'pipeline', label: 'Pipeline', tab: 'tokens' },
  { id: 'run',      label: 'Run',      tab: 'output' },
];

function tabToMode(tab: InspectorTab): 'pipeline' | 'run' {
  return tab === 'output' ? 'run' : 'pipeline';
}

interface InspectorNavProps {
  activeTab: InspectorTab;
  lastPipelineTab: PipelineTab;
  onTabChange: (tab: InspectorTab) => void;
}

export function InspectorNav({ activeTab, lastPipelineTab, onTabChange }: InspectorNavProps) {
  const mode = tabToMode(activeTab);

  return (
    <header className="inspector-nav">
      <div className="inspector-modes" role="tablist" aria-label="Inspector section">
        {MODES.map(m => (
          <button
            key={m.id}
            type="button"
            role="tab"
            aria-selected={mode === m.id}
            className={`inspector-mode-btn ${mode === m.id ? 'active' : ''}`}
            onClick={() => {
              if (m.id === 'pipeline') {
                onTabChange(mode === 'pipeline' ? activeTab : lastPipelineTab);
              } else {
                onTabChange('output');
              }
            }}
          >
            {m.label}
          </button>
        ))}
      </div>

      {mode === 'pipeline' && (
        <div className="inspector-subtabs" role="tablist" aria-label="Pipeline stage">
          {PIPELINE_TABS.map(t => (
            <button
              key={t.id}
              type="button"
              role="tab"
              aria-selected={activeTab === t.id}
              className={`inspector-subtab ${activeTab === t.id ? 'active' : ''}`}
              onClick={() => onTabChange(t.id)}
            >
              {t.label}
            </button>
          ))}
        </div>
      )}

      {mode === 'run' && (
        <p className="inspector-hint">
          Compile and run the code in the editor on the server. First request after idle
          may take ~30s.
        </p>
      )}
    </header>
  );
}
