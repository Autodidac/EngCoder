// Engineering missions are read-only here. The canonical list is maintained in
// docs/FUTURE_MISSIONS.md and compiled by scripts/sync_missions.py. Never mark
// a mission complete from a UI click without verification evidence.
(() => {
  const opener = document.getElementById('quickMissions');
  const dialog = document.getElementById('missionsDialog');
  const close = document.getElementById('closeMissions');
  const search = document.getElementById('missionSearch');
  const group = document.getElementById('missionPriority');
  const list = document.getElementById('missionList');
  const count = document.getElementById('missionCount');
  const caption = document.getElementById('missionCaption');
  const missions = typeof ENGCODER_OPEN_MISSIONS === 'undefined' ? [] : ENGCODER_OPEN_MISSIONS;
  if (!opener || !dialog || !list || !count || !caption) return;

  const create = (tag, cssClass, text) => {
    const node = document.createElement(tag);
    if (cssClass) node.className = cssClass;
    if (text !== undefined) node.textContent = String(text);
    return node;
  };
  const render = () => {
    const term = String(search?.value || '').trim().toLowerCase();
    const priority = group?.value || 'all';
    const visible = missions.filter(item =>
      (priority === 'all' || item.priority === priority) &&
      (!term || `${item.title} ${item.details} ${item.priority}`.toLowerCase().includes(term)));
    list.replaceChildren();
    for (const item of visible) {
      const row = create('li', 'mission-row');
      const head = create('div', 'mission-row-heading');
      head.append(create('span', 'mission-priority', item.priority), create('strong', '', item.title));
      row.append(head);
      if (item.details) row.append(create('p', '', item.details));
      list.append(row);
    }
    if (!visible.length) list.append(create('li', 'mission-empty', 'No matching missions.'));
    caption.textContent = `${visible.length} shown · ${missions.length} open · source: docs/FUTURE_MISSIONS.md`;
  };
  count.textContent = String(missions.length);
  opener.title = `Show ${missions.length} open engineering and future-information missions`;
  opener.addEventListener('click', () => {
    render();
    if (typeof dialog.showModal === 'function') {
      if (!dialog.open) dialog.showModal();
    } else {
      dialog.classList.add('open');
    }
    search?.focus();
  });
  close?.addEventListener('click', () => {
    if (dialog.open) dialog.close();
    else dialog.classList.remove('open');
  });
  search?.addEventListener('input', render);
  group?.addEventListener('change', render);
  render();
})();
