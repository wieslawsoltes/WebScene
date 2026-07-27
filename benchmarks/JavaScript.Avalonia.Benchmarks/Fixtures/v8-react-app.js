(function () {
  window.__webSceneReactProbe = { renders: 0, effects: 0 };
  class Toolbar extends React.Component {
    constructor(props) {
      super(props);
      this.state = { phase: 0 };
    }
    componentDidMount() {
      window.__webSceneReactProbe.effects++;
      this.setState(state => ({ phase: state.phase | 1 }));
      Promise.resolve().then(() => this.setState(state => ({ phase: state.phase | 2 })));
      this.mutationObserver = new MutationObserver(() => {
        this.setState(state => ({ phase: state.phase | 4 }));
      });
      this.mutationObserver.observe(this.node, { attributes: true });
      this.resizeObserver = new ResizeObserver(() => {
        this.setState(state => ({ phase: state.phase | 8 }));
      });
      this.resizeObserver.observe(this.node);
      this.node.setAttribute('data-mounted', 'true');
      parent.__webSceneOwnerSchedule(() => {
        this.setState(state => ({ phase: state.phase | 16 }));
      });
    }
    componentWillUnmount() {
      this.mutationObserver.disconnect();
      this.resizeObserver.disconnect();
    }
    render() {
      window.__webSceneReactProbe.renders++;
      parent.__webSceneOwnerRead(this.props.index);
      const children = [];
      const itemCount = Number(window.__webSceneReactProbeItemCount || 180);
      for (let index = 0; index < itemCount; index++) {
        children.push(React.createElement('span', { key: index }, 'item-' + index));
      }
      return React.createElement('section', null,
        React.createElement(
          'button',
          {
            'data-probe': this.props.index,
            ref: node => { this.node = node; },
            style: { width: (80 + this.props.index) + 'px', height: '24px' }
          },
          this.props.label + ':' + this.state.phase),
        React.createElement(
          'svg',
          {
            viewBox: '0 0 24 24',
            width: 24,
            height: 24,
            focusable: 'false',
            'aria-hidden': 'true',
            className: 'toolbar-icon'
          },
          React.createElement('defs', null,
            React.createElement('path', { id: 'probe-path-' + this.props.index, d: 'M2 12 L10 4 L22 20 Z' })),
          React.createElement('use', { href: '#probe-path-' + this.props.index }),
          React.createElement('circle', { cx: 12, cy: 12, r: 4, fill: 'currentColor' })),
        children);
    }
  }
  function mountToolbars() {
    for (let index = 0; index < 7; index++) {
      const mount = document.createElement('div');
      mount.className = 'probe-root';
      document.body.appendChild(mount);
      const element = React.createElement(Toolbar, {
        index: index,
        label: 'Toolbar-' + index
      });
      if (typeof ReactDOM.createRoot === 'function') ReactDOM.createRoot(mount).render(element);
      else ReactDOM.render(element, mount);
    }
  }
  class Launcher extends React.Component {
    constructor(props) {
      super(props);
      this.state = { chunkLoaded: false, styleLoaded: false };
    }
    render() {
      if (!window.__webSceneNestedRootsMounted) {
        window.__webSceneNestedRootsMounted = true;
        if (window.__webSceneReactSyncNestedRoots) mountToolbars();
        else queueMicrotask(mountToolbars);
        const renderStylesheet = document.createElement('link');
        renderStylesheet.rel = 'stylesheet';
        renderStylesheet.href = './Fixtures/v8-react-app.css';
        renderStylesheet.onload = () => this.setState({ styleLoaded: true });
        document.head.appendChild(renderStylesheet);
        const renderScript = document.createElement('script');
        renderScript.src = './Fixtures/v8-react-chunk.js';
        renderScript.onload = () => {
          window.__webSceneReactChunkOrder.push('load');
          this.setState({ chunkLoaded: true });
        };
        document.head.appendChild(renderScript);
        const nestedFrame = document.createElement('iframe');
        nestedFrame.src = URL.createObjectURL(new Blob([
          '<!doctype html><html><body><div id="nested-ready"></div></body></html>'
        ], { type: 'text/html' }));
        document.body.appendChild(nestedFrame);
      }
      return React.createElement(
        'div',
        { id: 'launcher-ready' },
        'launcher:' + Number(this.state.chunkLoaded) + Number(this.state.styleLoaded));
    }
  }
  const launcherMount = document.createElement('div');
  document.body.appendChild(launcherMount);
  ReactDOM.render(React.createElement(Launcher), launcherMount);
})();
