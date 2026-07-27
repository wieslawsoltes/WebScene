(function () {
  const state = window.__webSceneReactFocusState = {
    passiveRenders: 0,
    passiveEffects: 0,
    focusRenders: 0,
    focusMounts: 0,
    focusUpdates: 0,
    renderMicrotasks: 0,
    focusEvents: []
  };
  const contractInput = document.createElement('input');
  state.contract = {
    constructorType: typeof contractInput.constructor,
    constructorName: contractInput.constructor && contractInput.constructor.name,
    prototypeType: contractInput.constructor && typeof contractInput.constructor.prototype,
    prototypeIsNull: contractInput.constructor && contractInput.constructor.prototype === null,
    hasOwnPropertyType: typeof contractInput.hasOwnProperty,
    instanceOfInput: contractInput instanceof HTMLInputElement,
    prototypeMatches: Object.getPrototypeOf(contractInput) === HTMLInputElement.prototype
  };
  try {
    state.contract.hasOwnValue = contractInput.hasOwnProperty('value');
    const descriptor = Object.getOwnPropertyDescriptor(contractInput.constructor.prototype, 'value');
    state.contract.valueDescriptor = descriptor
      ? typeof descriptor.get + ':' + typeof descriptor.set
      : 'missing';
  } catch (error) {
    state.contract.descriptorError = String(error && error.stack || error);
  }
  try {
    const media = matchMedia('(min-width: 100px)');
    const mediaListener = function () {};
    media.addEventListener('change', mediaListener);
    media.removeEventListener('change', mediaListener);
    media.addListener(mediaListener);
    media.removeListener(mediaListener);
    state.contract.mediaQuery = typeof media.matches + ':' + String(media.matches) + ':ok';
  } catch (error) {
    state.contract.mediaQueryError = String(error && error.stack || error);
  }

  function PassiveProbe() {
    const value = React.useState(0);
    state.passiveRenders++;
    React.useEffect(function () {
      state.passiveEffects++;
      value[1](1);
    }, []);
    return React.createElement('span', { id: 'passive-probe-state' }, 'passive:' + value[0]);
  }

  class FocusProbe extends React.Component {
    constructor(props) {
      super(props);
      this.state = { phase: 0 };
    }

    componentDidMount() {
      state.focusMounts++;
      Promise.resolve().then(() => this.setState({ phase: 1 }));
    }

    componentDidUpdate() {
      state.focusUpdates++;
    }

    render() {
      state.focusRenders++;
      if (window.__webSceneReactScheduleFromRender && state.renderMicrotasks === 0) {
        state.renderMicrotasks++;
        Promise.resolve().then(function () {
          state.focusEvents.push('render-microtask');
          focusRoot.render(React.createElement(FocusProbe));
        });
      }
      return React.createElement('div', null,
        React.createElement('span', { id: 'focus-probe-state' }, 'focus:' + this.state.phase),
        React.createElement('input', {
          id: 'focus-probe-input',
          autoFocus: window.__webSceneReactUseAutoFocus,
          value: 'value-' + this.state.phase,
          onChange: function () {}
        }));
    }
  }

  const passiveMount = document.createElement('div');
  passiveMount.id = 'passive-probe-root';
  document.body.appendChild(passiveMount);

  const focusMount = document.createElement('div');
  focusMount.id = 'focus-probe-root';
  document.body.appendChild(focusMount);
  let focusRoot;
  let focusMicrotaskScheduled = false;

  document.addEventListener('focus', function (event) {
    if (event.target && event.target.id === 'focus-probe-input') {
      state.focusEvents.push('document-capture');
      if (!focusMicrotaskScheduled) {
        focusMicrotaskScheduled = true;
        Promise.resolve().then(function () {
          state.focusEvents.push('microtask');
          focusRoot.render(React.createElement(FocusProbe));
        });
      }
    }
  }, true);
  focusMount.addEventListener('focus', function (event) {
    if (event.target && event.target.id === 'focus-probe-input') {
      state.focusEvents.push('root-capture');
    }
  }, true);

  ReactDOM.createRoot(passiveMount).render(React.createElement(PassiveProbe));
  if (window.__webSceneReactLegacyFocusRoot) {
    focusRoot = { render: function (element) { ReactDOM.render(element, focusMount); } };
    ReactDOM.render(React.createElement(FocusProbe), focusMount);
  } else {
    focusRoot = ReactDOM.createRoot(focusMount);
    focusRoot.render(React.createElement(FocusProbe));
  }
})();
