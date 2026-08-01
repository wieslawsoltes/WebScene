// Copyright (c) WebScene contributors.
// Licensed under the MIT license.
//
// This is a clean-room html5ever TreeSink for WebScene. It intentionally
// contains no code from browser integrations reviewed during evaluation.

use html5ever::driver::parse_fragment_for_element;
use html5ever::interface::tree_builder::{ElementFlags, NodeOrText, QuirksMode, TreeSink};
use html5ever::tendril::{StrTendril, TendrilSink};
use html5ever::{parse_document, Attribute, ParseOpts, QualName};
use std::alloc::{GlobalAlloc, Layout, System};
use std::borrow::Cow;
use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

thread_local! {
    static ALLOCATION_CURRENT: Cell<u64> = const { Cell::new(0) };
    static ALLOCATION_PEAK: Cell<u64> = const { Cell::new(0) };
    static ALLOCATION_COUNT: Cell<u64> = const { Cell::new(0) };
}

struct MeasuringAllocator;

unsafe impl GlobalAlloc for MeasuringAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let allocation = unsafe { System.alloc(layout) };
        if !allocation.is_null() {
            let bytes = layout.size() as u64;
            ALLOCATION_COUNT.with(|value| value.set(value.get() + 1));
            ALLOCATION_CURRENT.with(|current| {
                let next = current.get() + bytes;
                current.set(next);
                ALLOCATION_PEAK.with(|peak| peak.set(peak.get().max(next)));
            });
        }
        allocation
    }

    unsafe fn dealloc(&self, allocation: *mut u8, layout: Layout) {
        ALLOCATION_CURRENT.with(|value| {
            value.set(value.get().saturating_sub(layout.size() as u64));
        });
        unsafe { System.dealloc(allocation, layout) };
    }

    unsafe fn realloc(&self, allocation: *mut u8, old_layout: Layout, new_size: usize) -> *mut u8 {
        let result = unsafe { System.realloc(allocation, old_layout, new_size) };
        if !result.is_null() {
            ALLOCATION_COUNT.with(|value| value.set(value.get() + 1));
            ALLOCATION_CURRENT.with(|current| {
                let next = current
                    .get()
                    .saturating_sub(old_layout.size() as u64)
                    .saturating_add(new_size as u64);
                current.set(next);
                ALLOCATION_PEAK.with(|peak| peak.set(peak.get().max(next)));
            });
        }
        result
    }
}

#[global_allocator]
static ALLOCATOR: MeasuringAllocator = MeasuringAllocator;

const ABI_VERSION: u32 = 1;
const STATUS_OK: u32 = 0;
const STATUS_INVALID_ARGUMENT: u32 = 1;
const STATUS_CALLBACK_FAILED: u32 = 2;
const STATUS_PANIC: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ByteSlice {
    pub data: *const u8,
    pub length: usize,
}

impl ByteSlice {
    fn from_bytes(value: &[u8]) -> Self {
        Self {
            data: value.as_ptr(),
            length: value.len(),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QualifiedName {
    pub namespace_uri: ByteSlice,
    pub local_name: ByteSlice,
    pub prefix: ByteSlice,
}

impl QualifiedName {
    fn from_name(value: &QualName) -> Self {
        Self {
            namespace_uri: ByteSlice::from_bytes(value.ns.as_bytes()),
            local_name: ByteSlice::from_bytes(value.local.as_bytes()),
            prefix: value
                .prefix
                .as_ref()
                .map(|prefix| ByteSlice::from_bytes(prefix.as_bytes()))
                .unwrap_or_default(),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ParserAttribute {
    pub name: QualifiedName,
    pub value: ByteSlice,
}

pub type NodeHandle = usize;

#[repr(C)]
pub struct SinkVTable {
    pub abi_version: u32,
    pub struct_size: u32,
    pub user_data: *mut c_void,
    pub document: NodeHandle,
    pub create_element: Option<
        unsafe extern "C" fn(
            *mut c_void,
            *const QualifiedName,
            *const ParserAttribute,
            usize,
        ) -> NodeHandle,
    >,
    pub create_comment: Option<unsafe extern "C" fn(*mut c_void, ByteSlice) -> NodeHandle>,
    pub create_processing_instruction:
        Option<unsafe extern "C" fn(*mut c_void, ByteSlice, ByteSlice) -> NodeHandle>,
    pub append_doctype:
        Option<unsafe extern "C" fn(*mut c_void, ByteSlice, ByteSlice, ByteSlice) -> NodeHandle>,
    pub append_node: Option<unsafe extern "C" fn(*mut c_void, NodeHandle, NodeHandle) -> u8>,
    pub append_text: Option<unsafe extern "C" fn(*mut c_void, NodeHandle, ByteSlice) -> u8>,
    pub insert_node_before: Option<unsafe extern "C" fn(*mut c_void, NodeHandle, NodeHandle) -> u8>,
    pub insert_text_before: Option<unsafe extern "C" fn(*mut c_void, NodeHandle, ByteSlice) -> u8>,
    pub append_node_based_on_parent:
        Option<unsafe extern "C" fn(*mut c_void, NodeHandle, NodeHandle, NodeHandle) -> u8>,
    pub append_text_based_on_parent:
        Option<unsafe extern "C" fn(*mut c_void, NodeHandle, NodeHandle, ByteSlice) -> u8>,
    pub remove_from_parent: Option<unsafe extern "C" fn(*mut c_void, NodeHandle) -> u8>,
    pub reparent_children: Option<unsafe extern "C" fn(*mut c_void, NodeHandle, NodeHandle) -> u8>,
    pub add_attrs_if_missing:
        Option<unsafe extern "C" fn(*mut c_void, NodeHandle, *const ParserAttribute, usize) -> u8>,
    pub get_template_contents: Option<unsafe extern "C" fn(*mut c_void, NodeHandle) -> NodeHandle>,
    pub set_quirks_mode: Option<unsafe extern "C" fn(*mut c_void, u32)>,
    pub parse_error: Option<unsafe extern "C" fn(*mut c_void, ByteSlice)>,
    pub callback_failed: Option<unsafe extern "C" fn(*mut c_void) -> u8>,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ParseOptions {
    pub abi_version: u32,
    pub struct_size: u32,
    pub scripting_enabled: u8,
    pub iframe_srcdoc: u8,
    pub exact_errors: u8,
    pub drop_doctype: u8,
    pub preserve_comments: u8,
    pub context_namespace: ByteSlice,
    pub context_local_name: ByteSlice,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ParseResult {
    pub status: u32,
    pub quirks_mode: u32,
    pub parse_error_count: u64,
    pub callback_count: u64,
    pub element_count: u64,
    pub text_append_count: u64,
    pub comment_count: u64,
    pub doctype_count: u64,
    pub rust_allocation_count: u64,
    pub rust_peak_bytes: u64,
    pub rust_retained_bytes: u64,
}

#[derive(Clone)]
struct ElementData {
    name: QualName,
    mathml_annotation_xml_integration_point: bool,
}

struct Sink<'a> {
    callbacks: &'a SinkVTable,
    // Boxed metadata keeps QualName addresses stable when the handle map
    // rehashes. TreeSink::elem_name returns a borrow that may outlive the
    // RefCell map guard, but entries are never replaced during a parse.
    elements: RefCell<HashMap<NodeHandle, Box<ElementData>>>,
    quirks_mode: Cell<QuirksMode>,
    errors: Cell<u64>,
    callback_count: Cell<u64>,
    element_count: Cell<u64>,
    text_append_count: Cell<u64>,
    comment_count: Cell<u64>,
    doctype_count: Cell<u64>,
    preserve_comments: bool,
}

impl<'a> Sink<'a> {
    fn callback(&self) {
        self.callback_count.set(self.callback_count.get() + 1);
    }

    fn attributes(attrs: &[Attribute]) -> Vec<ParserAttribute> {
        attrs
            .iter()
            .map(|attribute| ParserAttribute {
                name: QualifiedName::from_name(&attribute.name),
                value: ByteSlice::from_bytes(attribute.value.as_bytes()),
            })
            .collect()
    }

    fn append_value(&self, parent: NodeHandle, child: NodeOrText<NodeHandle>) {
        match child {
            NodeOrText::AppendNode(node) => {
                self.callback();
                if let Some(callback) = self.callbacks.append_node {
                    unsafe { callback(self.callbacks.user_data, parent, node) };
                }
            }
            NodeOrText::AppendText(text) => {
                self.callback();
                self.text_append_count.set(self.text_append_count.get() + 1);
                if let Some(callback) = self.callbacks.append_text {
                    unsafe {
                        callback(
                            self.callbacks.user_data,
                            parent,
                            ByteSlice::from_bytes(text.as_bytes()),
                        )
                    };
                }
            }
        }
    }

    fn insert_value(&self, sibling: NodeHandle, child: NodeOrText<NodeHandle>) {
        match child {
            NodeOrText::AppendNode(node) => {
                self.callback();
                if let Some(callback) = self.callbacks.insert_node_before {
                    unsafe { callback(self.callbacks.user_data, sibling, node) };
                }
            }
            NodeOrText::AppendText(text) => {
                self.callback();
                self.text_append_count.set(self.text_append_count.get() + 1);
                if let Some(callback) = self.callbacks.insert_text_before {
                    unsafe {
                        callback(
                            self.callbacks.user_data,
                            sibling,
                            ByteSlice::from_bytes(text.as_bytes()),
                        )
                    };
                }
            }
        }
    }
}

impl TreeSink for Sink<'_> {
    type Handle = NodeHandle;
    type Output = ParseResult;
    type ElemName<'a>
        = &'a QualName
    where
        Self: 'a;

    fn finish(self) -> Self::Output {
        ParseResult {
            status: if self.callback_failed() {
                STATUS_CALLBACK_FAILED
            } else {
                STATUS_OK
            },
            quirks_mode: quirks_code(self.quirks_mode.get()),
            parse_error_count: self.errors.get(),
            callback_count: self.callback_count.get(),
            element_count: self.element_count.get(),
            text_append_count: self.text_append_count.get(),
            comment_count: self.comment_count.get(),
            doctype_count: self.doctype_count.get(),
            rust_allocation_count: 0,
            rust_peak_bytes: 0,
            rust_retained_bytes: 0,
        }
    }

    fn parse_error(&self, message: Cow<'static, str>) {
        self.errors.set(self.errors.get() + 1);
        self.callback();
        if let Some(callback) = self.callbacks.parse_error {
            unsafe {
                callback(
                    self.callbacks.user_data,
                    ByteSlice::from_bytes(message.as_bytes()),
                )
            };
        }
    }

    fn get_document(&self) -> Self::Handle {
        self.callbacks.document
    }

    fn set_quirks_mode(&self, mode: QuirksMode) {
        self.quirks_mode.set(mode);
        self.callback();
        if let Some(callback) = self.callbacks.set_quirks_mode {
            unsafe { callback(self.callbacks.user_data, quirks_code(mode)) };
        }
    }

    fn same_node(&self, left: &Self::Handle, right: &Self::Handle) -> bool {
        left == right
    }

    fn elem_name(&self, target: &Self::Handle) -> Self::ElemName<'_> {
        // TreeSink requires a borrowed name. Element metadata lives for the
        // complete parse, so extending this borrow to the sink lifetime is
        // safe; entries are never removed or replaced.
        let elements = self.elements.borrow();
        let name = &elements
            .get(target)
            .expect("html5ever requested the name of a non-element")
            .name as *const QualName;
        unsafe { &*name }
    }

    fn create_element(
        &self,
        name: QualName,
        attrs: Vec<Attribute>,
        flags: ElementFlags,
    ) -> Self::Handle {
        let attributes = Self::attributes(&attrs);
        self.callback();
        let handle = self
            .callbacks
            .create_element
            .map(|callback| unsafe {
                callback(
                    self.callbacks.user_data,
                    &QualifiedName::from_name(&name),
                    attributes.as_ptr(),
                    attributes.len(),
                )
            })
            .unwrap_or_default();
        self.elements.borrow_mut().insert(
            handle,
            Box::new(ElementData {
                name,
                mathml_annotation_xml_integration_point: flags
                    .mathml_annotation_xml_integration_point,
            }),
        );
        self.element_count.set(self.element_count.get() + 1);
        handle
    }

    fn create_comment(&self, text: StrTendril) -> Self::Handle {
        if !self.preserve_comments {
            return 0;
        }
        self.callback();
        self.comment_count.set(self.comment_count.get() + 1);
        self.callbacks
            .create_comment
            .map(|callback| unsafe {
                callback(
                    self.callbacks.user_data,
                    ByteSlice::from_bytes(text.as_bytes()),
                )
            })
            .unwrap_or_default()
    }

    fn create_pi(&self, target: StrTendril, data: StrTendril) -> Self::Handle {
        self.callback();
        self.callbacks
            .create_processing_instruction
            .map(|callback| unsafe {
                callback(
                    self.callbacks.user_data,
                    ByteSlice::from_bytes(target.as_bytes()),
                    ByteSlice::from_bytes(data.as_bytes()),
                )
            })
            .unwrap_or_default()
    }

    fn append(&self, parent: &Self::Handle, child: NodeOrText<Self::Handle>) {
        // A zero handle is the intentional representation of a discarded
        // comment. Ignore its later append rather than requiring a fork.
        if matches!(child, NodeOrText::AppendNode(0)) {
            return;
        }
        self.append_value(*parent, child);
    }

    fn append_before_sibling(&self, sibling: &Self::Handle, child: NodeOrText<Self::Handle>) {
        if matches!(child, NodeOrText::AppendNode(0)) {
            return;
        }
        self.insert_value(*sibling, child);
    }

    fn append_based_on_parent_node(
        &self,
        element: &Self::Handle,
        previous_element: &Self::Handle,
        child: NodeOrText<Self::Handle>,
    ) {
        match child {
            NodeOrText::AppendNode(node) => {
                if node == 0 {
                    return;
                }
                self.callback();
                if let Some(callback) = self.callbacks.append_node_based_on_parent {
                    unsafe {
                        callback(self.callbacks.user_data, *element, *previous_element, node)
                    };
                }
            }
            NodeOrText::AppendText(text) => {
                self.callback();
                self.text_append_count.set(self.text_append_count.get() + 1);
                if let Some(callback) = self.callbacks.append_text_based_on_parent {
                    unsafe {
                        callback(
                            self.callbacks.user_data,
                            *element,
                            *previous_element,
                            ByteSlice::from_bytes(text.as_bytes()),
                        )
                    };
                }
            }
        }
    }

    fn append_doctype_to_document(
        &self,
        name: StrTendril,
        public_id: StrTendril,
        system_id: StrTendril,
    ) {
        self.callback();
        self.doctype_count.set(self.doctype_count.get() + 1);
        if let Some(callback) = self.callbacks.append_doctype {
            unsafe {
                callback(
                    self.callbacks.user_data,
                    ByteSlice::from_bytes(name.as_bytes()),
                    ByteSlice::from_bytes(public_id.as_bytes()),
                    ByteSlice::from_bytes(system_id.as_bytes()),
                )
            };
        }
    }

    fn add_attrs_if_missing(&self, target: &Self::Handle, attrs: Vec<Attribute>) {
        let attributes = Self::attributes(&attrs);
        self.callback();
        if let Some(callback) = self.callbacks.add_attrs_if_missing {
            unsafe {
                callback(
                    self.callbacks.user_data,
                    *target,
                    attributes.as_ptr(),
                    attributes.len(),
                )
            };
        }
    }

    fn remove_from_parent(&self, target: &Self::Handle) {
        self.callback();
        if let Some(callback) = self.callbacks.remove_from_parent {
            unsafe { callback(self.callbacks.user_data, *target) };
        }
    }

    fn reparent_children(&self, node: &Self::Handle, new_parent: &Self::Handle) {
        self.callback();
        if let Some(callback) = self.callbacks.reparent_children {
            unsafe { callback(self.callbacks.user_data, *node, *new_parent) };
        }
    }

    fn get_template_contents(&self, target: &Self::Handle) -> Self::Handle {
        self.callback();
        self.callbacks
            .get_template_contents
            .map(|callback| unsafe { callback(self.callbacks.user_data, *target) })
            .unwrap_or(*target)
    }

    fn is_mathml_annotation_xml_integration_point(&self, target: &Self::Handle) -> bool {
        self.elements
            .borrow()
            .get(target)
            .map(|element| element.mathml_annotation_xml_integration_point)
            .unwrap_or(false)
    }
}

impl Sink<'_> {
    fn callback_failed(&self) -> bool {
        self.callbacks
            .callback_failed
            .map(|callback| unsafe { callback(self.callbacks.user_data) != 0 })
            .unwrap_or(false)
    }
}

fn quirks_code(mode: QuirksMode) -> u32 {
    match mode {
        QuirksMode::NoQuirks => 0,
        QuirksMode::LimitedQuirks => 1,
        QuirksMode::Quirks => 2,
    }
}

fn read_slice(value: ByteSlice) -> Option<&'static [u8]> {
    if value.length == 0 {
        return Some(&[]);
    }
    if value.data.is_null() {
        return None;
    }
    Some(unsafe { std::slice::from_raw_parts(value.data, value.length) })
}

fn parse_options(options: &ParseOptions) -> ParseOpts {
    let mut result = ParseOpts::default();
    result.tree_builder.scripting_enabled = options.scripting_enabled != 0;
    result.tree_builder.iframe_srcdoc = options.iframe_srcdoc != 0;
    result.tree_builder.exact_errors = options.exact_errors != 0;
    result.tree_builder.drop_doctype = options.drop_doctype != 0;
    result
}

fn validate<'a>(
    input: ByteSlice,
    options: *const ParseOptions,
    callbacks: *const SinkVTable,
) -> Result<(&'a [u8], &'a ParseOptions, &'a SinkVTable), ParseResult> {
    let Some(input) = read_slice(input) else {
        return Err(ParseResult {
            status: STATUS_INVALID_ARGUMENT,
            ..Default::default()
        });
    };
    let Some(options) = (unsafe { options.as_ref() }) else {
        return Err(ParseResult {
            status: STATUS_INVALID_ARGUMENT,
            ..Default::default()
        });
    };
    let Some(callbacks) = (unsafe { callbacks.as_ref() }) else {
        return Err(ParseResult {
            status: STATUS_INVALID_ARGUMENT,
            ..Default::default()
        });
    };
    if options.abi_version != ABI_VERSION
        || callbacks.abi_version != ABI_VERSION
        || options.struct_size < std::mem::size_of::<ParseOptions>() as u32
        || callbacks.struct_size < std::mem::size_of::<SinkVTable>() as u32
        || callbacks.document == 0
        || callbacks.create_element.is_none()
        || callbacks.append_node.is_none()
        || callbacks.append_text.is_none()
    {
        return Err(ParseResult {
            status: STATUS_INVALID_ARGUMENT,
            ..Default::default()
        });
    }
    Ok((input, options, callbacks))
}

fn new_sink<'a>(options: &ParseOptions, callbacks: &'a SinkVTable) -> Sink<'a> {
    Sink {
        callbacks,
        elements: RefCell::new(HashMap::new()),
        quirks_mode: Cell::new(QuirksMode::NoQuirks),
        errors: Cell::new(0),
        callback_count: Cell::new(0),
        element_count: Cell::new(0),
        text_append_count: Cell::new(0),
        comment_count: Cell::new(0),
        doctype_count: Cell::new(0),
        preserve_comments: options.preserve_comments != 0,
    }
}

fn reset_allocation_metrics() {
    ALLOCATION_CURRENT.with(|value| value.set(0));
    ALLOCATION_PEAK.with(|value| value.set(0));
    ALLOCATION_COUNT.with(|value| value.set(0));
}

fn attach_allocation_metrics(mut result: ParseResult) -> ParseResult {
    result.rust_allocation_count = ALLOCATION_COUNT.with(Cell::get);
    result.rust_peak_bytes = ALLOCATION_PEAK.with(Cell::get);
    result.rust_retained_bytes = ALLOCATION_CURRENT.with(Cell::get);
    result
}

#[no_mangle]
pub extern "C" fn webscene_html_parser_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub extern "C" fn webscene_html_parse_document(
    input: ByteSlice,
    options: *const ParseOptions,
    callbacks: *const SinkVTable,
) -> ParseResult {
    let validated = validate(input, options, callbacks);
    let (input, options, callbacks) = match validated {
        Ok(value) => value,
        Err(error) => return error,
    };
    reset_allocation_metrics();
    let result = catch_unwind(AssertUnwindSafe(|| {
        parse_document(new_sink(options, callbacks), parse_options(options))
            .from_utf8()
            .one(input)
    }))
    .unwrap_or(ParseResult {
        status: STATUS_PANIC,
        ..Default::default()
    });
    attach_allocation_metrics(result)
}

#[no_mangle]
pub extern "C" fn webscene_html_parse_fragment(
    input: ByteSlice,
    options: *const ParseOptions,
    callbacks: *const SinkVTable,
) -> ParseResult {
    let validated = validate(input, options, callbacks);
    let (input, options, callbacks) = match validated {
        Ok(value) => value,
        Err(error) => return error,
    };
    reset_allocation_metrics();
    let result = catch_unwind(AssertUnwindSafe(|| {
        let namespace = read_slice(options.context_namespace)
            .and_then(|bytes| std::str::from_utf8(bytes).ok())
            .filter(|value| !value.is_empty())
            .unwrap_or("http://www.w3.org/1999/xhtml");
        let local_name = read_slice(options.context_local_name)
            .and_then(|bytes| std::str::from_utf8(bytes).ok())
            .filter(|value| !value.is_empty())
            .unwrap_or("body");
        let context_name = QualName::new(None, namespace.into(), local_name.into());
        // The synthetic context never crosses the ABI. Use a reserved handle
        // and retain only the QualName metadata html5ever asks the sink for.
        let context_handle = usize::MAX;
        let sink = new_sink(options, callbacks);
        sink.elements.borrow_mut().insert(
            context_handle,
            Box::new(ElementData {
                name: context_name.clone(),
                mathml_annotation_xml_integration_point: false,
            }),
        );
        parse_fragment_for_element(
            sink,
            parse_options(options),
            context_handle,
            options.scripting_enabled != 0,
            None,
        )
        .from_utf8()
        .one(input)
    }))
    .unwrap_or(ParseResult {
        status: STATUS_PANIC,
        ..Default::default()
    });
    attach_allocation_metrics(result)
}

#[cfg(feature = "cssparser")]
mod css_syntax {
    use super::*;
    use cssparser::{
        parse_important, AtRuleParser, BasicParseErrorKind, CowRcStr, DeclarationParser,
        ParseError, Parser, ParserInput, ParserState, QualifiedRuleParser, RuleBodyItemParser,
        RuleBodyParser, StyleSheetParser, Token,
    };

    const CSS_RULE_STYLE: u32 = 0;
    const CSS_RULE_AT: u32 = 1;
    const CSS_NO_PARENT: usize = usize::MAX;

    #[derive(Default)]
    struct CssDeclaration {
        name: String,
        value: String,
        important: bool,
    }

    enum CssRule {
        Style {
            prelude: String,
            declarations: Vec<CssDeclaration>,
        },
        At {
            name: String,
            prelude: String,
            has_block: bool,
            declarations: Vec<CssDeclaration>,
            children: Vec<CssRule>,
        },
    }

    struct CssAtPrelude {
        name: String,
        prelude: String,
    }

    enum CssBodyItem {
        Declaration(CssDeclaration),
        Ignored,
    }

    #[derive(Clone)]
    struct CssSyntaxParser {
        errors: std::rc::Rc<Cell<u64>>,
    }

    fn trim_css_whitespace(value: &str) -> &str {
        value.trim_matches(|character| matches!(character, ' ' | '\t' | '\n' | '\r' | '\x0c'))
    }

    fn consume_raw<'i>(input: &mut Parser<'i, '_>) -> String {
        let start = input.position();
        while input.next_including_whitespace_and_comments().is_ok() {}
        trim_css_whitespace(input.slice_from(start)).to_string()
    }

    fn consume_declaration_value<'i>(input: &mut Parser<'i, '_>) -> (String, bool) {
        let start = input.position();
        let end;
        let mut important = false;
        loop {
            let state = input.state();
            let token = match input.next_including_whitespace_and_comments() {
                Ok(token) => token.clone(),
                Err(_) => {
                    end = input.position();
                    break;
                }
            };
            if token == Token::Delim('!') {
                input.reset(&state);
                if input.try_parse(parse_important).is_ok() && input.is_exhausted() {
                    end = state.position();
                    important = true;
                    break;
                }
                input.reset(&state);
                let _ = input.next_including_whitespace_and_comments();
            }
        }
        (
            trim_css_whitespace(input.slice(start..end)).to_string(),
            important,
        )
    }

    impl<'i> DeclarationParser<'i> for CssSyntaxParser {
        type Declaration = CssBodyItem;
        type Error = ();

        fn parse_value<'t>(
            &mut self,
            name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
            _declaration_start: &ParserState,
        ) -> Result<Self::Declaration, ParseError<'i, Self::Error>> {
            let (value, important) = consume_declaration_value(input);
            let name = if name.starts_with("--") {
                name.to_string()
            } else {
                name.to_ascii_lowercase()
            };
            Ok(CssBodyItem::Declaration(CssDeclaration {
                name,
                value,
                important,
            }))
        }
    }

    impl<'i> AtRuleParser<'i> for CssSyntaxParser {
        type Prelude = CssAtPrelude;
        type AtRule = CssRule;
        type Error = ();

        fn parse_prelude<'t>(
            &mut self,
            name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::Prelude, ParseError<'i, Self::Error>> {
            Ok(CssAtPrelude {
                name: name.to_ascii_lowercase(),
                prelude: consume_raw(input),
            })
        }

        fn rule_without_block(
            &mut self,
            prelude: Self::Prelude,
            _start: &ParserState,
        ) -> Result<Self::AtRule, ()> {
            Ok(CssRule::At {
                name: prelude.name,
                prelude: prelude.prelude,
                has_block: false,
                declarations: Vec::new(),
                children: Vec::new(),
            })
        }

        fn parse_block<'t>(
            &mut self,
            prelude: Self::Prelude,
            _start: &ParserState,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::AtRule, ParseError<'i, Self::Error>> {
            let mut declarations = Vec::new();
            let mut children = Vec::new();
            if prelude.name.eq_ignore_ascii_case("font-face")
                || prelude.name.eq_ignore_ascii_case("page")
            {
                declarations = parse_css_declaration_list(input, self.clone());
            } else if prelude.name.eq_ignore_ascii_case("media")
                || prelude.name.eq_ignore_ascii_case("supports")
                || prelude.name.eq_ignore_ascii_case("layer")
                || prelude.name.eq_ignore_ascii_case("container")
                || prelude.name.eq_ignore_ascii_case("keyframes")
                || prelude.name.eq_ignore_ascii_case("-webkit-keyframes")
            {
                children = parse_css_rule_list(input, self.clone());
            } else {
                let _ = consume_raw(input);
            }
            Ok(CssRule::At {
                name: prelude.name,
                prelude: prelude.prelude,
                has_block: true,
                declarations,
                children,
            })
        }
    }

    impl<'i> QualifiedRuleParser<'i> for CssSyntaxParser {
        type Prelude = String;
        type QualifiedRule = CssRule;
        type Error = ();

        fn parse_prelude<'t>(
            &mut self,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::Prelude, ParseError<'i, Self::Error>> {
            let prelude = consume_raw(input);
            if prelude.is_empty() {
                return Err(input.new_error(BasicParseErrorKind::QualifiedRuleInvalid));
            }
            Ok(prelude)
        }

        fn parse_block<'t>(
            &mut self,
            prelude: Self::Prelude,
            _start: &ParserState,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::QualifiedRule, ParseError<'i, Self::Error>> {
            Ok(CssRule::Style {
                prelude,
                declarations: parse_css_declaration_list(input, self.clone()),
            })
        }
    }

    impl<'i> AtRuleParser<'i> for CssDeclarationListParser {
        type Prelude = ();
        type AtRule = CssBodyItem;
        type Error = ();

        fn parse_prelude<'t>(
            &mut self,
            _name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::Prelude, ParseError<'i, Self::Error>> {
            let _ = consume_raw(input);
            Ok(())
        }

        fn rule_without_block(
            &mut self,
            _prelude: Self::Prelude,
            _start: &ParserState,
        ) -> Result<Self::AtRule, ()> {
            Ok(CssBodyItem::Ignored)
        }

        fn parse_block<'t>(
            &mut self,
            _prelude: Self::Prelude,
            _start: &ParserState,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::AtRule, ParseError<'i, Self::Error>> {
            let _ = consume_raw(input);
            Ok(CssBodyItem::Ignored)
        }
    }

    impl<'i> QualifiedRuleParser<'i> for CssDeclarationListParser {
        type Prelude = ();
        type QualifiedRule = CssBodyItem;
        type Error = ();
    }

    struct CssDeclarationListParser {
        parser: CssSyntaxParser,
    }

    impl<'i> DeclarationParser<'i> for CssDeclarationListParser {
        type Declaration = CssBodyItem;
        type Error = ();

        fn parse_value<'t>(
            &mut self,
            name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
            declaration_start: &ParserState,
        ) -> Result<Self::Declaration, ParseError<'i, Self::Error>> {
            self.parser.parse_value(name, input, declaration_start)
        }
    }

    impl RuleBodyItemParser<'_, CssBodyItem, ()> for CssDeclarationListParser {
        fn parse_declarations(&self) -> bool {
            true
        }

        fn parse_qualified(&self) -> bool {
            false
        }
    }

    fn parse_css_declaration_list<'i>(
        input: &mut Parser<'i, '_>,
        parser: CssSyntaxParser,
    ) -> Vec<CssDeclaration> {
        let errors = parser.errors.clone();
        let mut body_parser = CssDeclarationListParser { parser };
        RuleBodyParser::new(input, &mut body_parser)
            .filter_map(|item| match item {
                Ok(CssBodyItem::Declaration(declaration)) => Some(declaration),
                Ok(CssBodyItem::Ignored) => None,
                Err(_) => {
                    errors.set(errors.get() + 1);
                    None
                }
            })
            .collect()
    }

    fn parse_css_rule_list<'i>(
        input: &mut Parser<'i, '_>,
        mut parser: CssSyntaxParser,
    ) -> Vec<CssRule> {
        let errors = parser.errors.clone();
        StyleSheetParser::new(input, &mut parser)
            .filter_map(|item| match item {
                Ok(rule) => Some(rule),
                Err(_) => {
                    errors.set(errors.get() + 1);
                    None
                }
            })
            .collect()
    }

    struct FlatCssRule {
        kind: u32,
        parent_index: usize,
        name: String,
        prelude: String,
        first_declaration: usize,
        declaration_count: usize,
        has_block: bool,
    }

    #[derive(Default)]
    struct CssSyntaxOutput {
        rules: Vec<FlatCssRule>,
        declarations: Vec<CssDeclaration>,
    }

    fn flatten_css_rule(rule: CssRule, parent_index: usize, output: &mut CssSyntaxOutput) {
        let rule_index = output.rules.len();
        let (kind, name, prelude, has_block, declarations, children) = match rule {
            CssRule::Style {
                prelude,
                declarations,
            } => (
                CSS_RULE_STYLE,
                String::new(),
                prelude,
                true,
                declarations,
                Vec::new(),
            ),
            CssRule::At {
                name,
                prelude,
                has_block,
                declarations,
                children,
            } => (
                CSS_RULE_AT,
                name,
                prelude,
                has_block,
                declarations,
                children,
            ),
        };
        let first_declaration = output.declarations.len();
        let declaration_count = declarations.len();
        output.declarations.extend(declarations);
        output.rules.push(FlatCssRule {
            kind,
            parent_index,
            name,
            prelude,
            first_declaration,
            declaration_count,
            has_block,
        });
        for child in children {
            flatten_css_rule(child, rule_index, output);
        }
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct CssParseResult {
        pub status: u32,
        pub parse_error_count: u64,
        pub rule_count: u64,
        pub declaration_count: u64,
        pub rust_allocation_count: u64,
        pub rust_peak_bytes: u64,
        pub rust_retained_bytes: u64,
        pub handle: *mut c_void,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct CssRuleView {
        pub kind: u32,
        pub has_block: u8,
        pub parent_index: usize,
        pub name: ByteSlice,
        pub prelude: ByteSlice,
        pub first_declaration: usize,
        pub declaration_count: usize,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct CssDeclarationView {
        pub name: ByteSlice,
        pub value: ByteSlice,
        pub important: u8,
    }

    fn css_error(status: u32) -> CssParseResult {
        CssParseResult {
            status,
            ..Default::default()
        }
    }

    fn finish_css_parse(output: CssSyntaxOutput, parse_error_count: u64) -> CssParseResult {
        let rule_count = output.rules.len() as u64;
        let declaration_count = output.declarations.len() as u64;
        let handle = Box::into_raw(Box::new(output)).cast::<c_void>();
        CssParseResult {
            status: STATUS_OK,
            parse_error_count,
            rule_count,
            declaration_count,
            rust_allocation_count: ALLOCATION_COUNT.with(Cell::get),
            rust_peak_bytes: ALLOCATION_PEAK.with(Cell::get),
            rust_retained_bytes: ALLOCATION_CURRENT.with(Cell::get),
            handle,
        }
    }

    fn css_input(input: ByteSlice) -> Result<&'static str, CssParseResult> {
        read_slice(input)
            .and_then(|bytes| std::str::from_utf8(bytes).ok())
            .ok_or_else(|| css_error(STATUS_INVALID_ARGUMENT))
    }

    #[no_mangle]
    pub extern "C" fn webscene_css_parse_stylesheet(input: ByteSlice) -> CssParseResult {
        let input = match css_input(input) {
            Ok(input) => input,
            Err(error) => return error,
        };
        reset_allocation_metrics();
        catch_unwind(AssertUnwindSafe(|| {
            let errors = std::rc::Rc::new(Cell::new(0));
            let parser = CssSyntaxParser {
                errors: errors.clone(),
            };
            let mut parser_input = ParserInput::new(input);
            let mut parser_input = Parser::new(&mut parser_input);
            let rules = parse_css_rule_list(&mut parser_input, parser);
            let mut output = CssSyntaxOutput::default();
            for rule in rules {
                flatten_css_rule(rule, CSS_NO_PARENT, &mut output);
            }
            finish_css_parse(output, errors.get())
        }))
        .unwrap_or_else(|_| css_error(STATUS_PANIC))
    }

    #[no_mangle]
    pub extern "C" fn webscene_css_parser_abi_version() -> u32 {
        ABI_VERSION
    }

    #[no_mangle]
    pub extern "C" fn webscene_css_parse_declarations(input: ByteSlice) -> CssParseResult {
        let input = match css_input(input) {
            Ok(input) => input,
            Err(error) => return error,
        };
        reset_allocation_metrics();
        catch_unwind(AssertUnwindSafe(|| {
            let errors = std::rc::Rc::new(Cell::new(0));
            let parser = CssSyntaxParser {
                errors: errors.clone(),
            };
            let mut parser_input = ParserInput::new(input);
            let mut parser_input = Parser::new(&mut parser_input);
            let declarations = parse_css_declaration_list(&mut parser_input, parser);
            finish_css_parse(
                CssSyntaxOutput {
                    rules: Vec::new(),
                    declarations,
                },
                errors.get(),
            )
        }))
        .unwrap_or_else(|_| css_error(STATUS_PANIC))
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_css_rule_at(
        handle: *const c_void,
        index: usize,
        output: *mut CssRuleView,
    ) -> u8 {
        let Some(handle) = (unsafe { (handle.cast::<CssSyntaxOutput>()).as_ref() }) else {
            return 0;
        };
        let Some(output) = (unsafe { output.as_mut() }) else {
            return 0;
        };
        let Some(rule) = handle.rules.get(index) else {
            return 0;
        };
        *output = CssRuleView {
            kind: rule.kind,
            has_block: u8::from(rule.has_block),
            parent_index: rule.parent_index,
            name: ByteSlice::from_bytes(rule.name.as_bytes()),
            prelude: ByteSlice::from_bytes(rule.prelude.as_bytes()),
            first_declaration: rule.first_declaration,
            declaration_count: rule.declaration_count,
        };
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_css_declaration_at(
        handle: *const c_void,
        index: usize,
        output: *mut CssDeclarationView,
    ) -> u8 {
        let Some(handle) = (unsafe { (handle.cast::<CssSyntaxOutput>()).as_ref() }) else {
            return 0;
        };
        let Some(output) = (unsafe { output.as_mut() }) else {
            return 0;
        };
        let Some(declaration) = handle.declarations.get(index) else {
            return 0;
        };
        *output = CssDeclarationView {
            name: ByteSlice::from_bytes(declaration.name.as_bytes()),
            value: ByteSlice::from_bytes(declaration.value.as_bytes()),
            important: u8::from(declaration.important),
        };
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_css_free(handle: *mut c_void) {
        if !handle.is_null() {
            drop(unsafe { Box::from_raw(handle.cast::<CssSyntaxOutput>()) });
        }
    }
}
