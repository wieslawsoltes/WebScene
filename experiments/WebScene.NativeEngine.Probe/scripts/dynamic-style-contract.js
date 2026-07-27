(() => {
  document.body.innerHTML = '<div id="dynamic-style-target">target</div>';

  const stylesheet = document.createElement('style');
  stylesheet.textContent = `
    #dynamic-style-target {
      width: 321px;
      height: 37px;
      background: #123456;
    }
  `;
  document.body.appendChild(stylesheet);

  const target = document.getElementById('dynamic-style-target');
  const targetRect = target.getBoundingClientRect();
  const stylesheetRect = stylesheet.getBoundingClientRect();
  if (targetRect.width !== 321 || targetRect.height !== 37) {
    throw new Error(
      `Dynamic stylesheet was not applied: ${targetRect.width}x${targetRect.height}`);
  }
  if (getComputedStyle(stylesheet).display !== 'none'
      || stylesheetRect.width !== 0
      || stylesheetRect.height !== 0) {
    throw new Error(
      `Style element generated a box: ${stylesheetRect.width}x${stylesheetRect.height}`);
  }
})();
