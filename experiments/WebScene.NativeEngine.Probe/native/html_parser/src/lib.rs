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

    fn trim_css_whitespace(value: &str) -> &str {
        value.trim_matches(|character| matches!(character, ' ' | '\t' | '\n' | '\r' | '\x0c'))
    }

    fn consume_raw<'i>(input: &mut Parser<'i, '_>) -> &'i str {
        let start = input.position();
        while input.next_including_whitespace_and_comments().is_ok() {}
        trim_css_whitespace(input.slice_from(start))
    }

    fn consume_declaration_value<'i>(input: &mut Parser<'i, '_>) -> (&'i str, bool) {
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
        (trim_css_whitespace(input.slice(start..end)), important)
    }

    fn css_input(input: ByteSlice) -> Option<&'static str> {
        read_slice(input).and_then(|bytes| std::str::from_utf8(bytes).ok())
    }

    type CssBeginRuleCallback =
        unsafe extern "C" fn(*mut c_void, u32, u8, usize, ByteSlice, ByteSlice, *mut usize) -> u8;
    type CssDeclarationCallback = unsafe extern "C" fn(*mut c_void, ByteSlice, ByteSlice, u8) -> u8;
    type CssEndRuleCallback = unsafe extern "C" fn(*mut c_void, usize, usize) -> u8;

    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct CssSinkVTable {
        begin_rule: Option<CssBeginRuleCallback>,
        declaration: Option<CssDeclarationCallback>,
        end_rule: Option<CssEndRuleCallback>,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct CssStreamResult {
        status: u32,
        parse_error_count: u64,
        rule_count: u64,
        declaration_count: u64,
        rust_allocation_count: u64,
        rust_peak_bytes: u64,
        rust_retained_bytes: u64,
    }

    #[derive(Clone)]
    struct CssStreamState {
        callbacks: CssSinkVTable,
        context: *mut c_void,
        errors: std::rc::Rc<Cell<u64>>,
        callback_failed: std::rc::Rc<Cell<bool>>,
        rule_count: std::rc::Rc<Cell<u64>>,
        declaration_count: std::rc::Rc<Cell<u64>>,
    }

    impl CssStreamState {
        fn begin_rule(
            &self,
            kind: u32,
            has_block: bool,
            parent_index: usize,
            name: &str,
            prelude: &str,
        ) -> usize {
            let mut index = usize::MAX;
            let accepted = self.callbacks.begin_rule.is_some_and(|callback| unsafe {
                callback(
                    self.context,
                    kind,
                    u8::from(has_block),
                    parent_index,
                    ByteSlice::from_bytes(name.as_bytes()),
                    ByteSlice::from_bytes(prelude.as_bytes()),
                    &mut index,
                ) != 0
            });
            if accepted {
                self.rule_count.set(self.rule_count.get() + 1);
            } else {
                self.callback_failed.set(true);
            }
            index
        }

        fn declaration(&self, name: &str, value: &str, important: bool) {
            let accepted = self.callbacks.declaration.is_some_and(|callback| unsafe {
                callback(
                    self.context,
                    ByteSlice::from_bytes(name.as_bytes()),
                    ByteSlice::from_bytes(value.as_bytes()),
                    u8::from(important),
                ) != 0
            });
            if accepted {
                self.declaration_count.set(self.declaration_count.get() + 1);
            } else {
                self.callback_failed.set(true);
            }
        }

        fn end_rule(&self, rule_index: usize, declaration_count: usize) {
            let accepted = self.callbacks.end_rule.is_some_and(|callback| unsafe {
                callback(self.context, rule_index, declaration_count) != 0
            });
            if !accepted {
                self.callback_failed.set(true);
            }
        }
    }

    struct CssStreamPrelude<'i> {
        name: CowRcStr<'i>,
        prelude: &'i str,
    }

    #[derive(Clone)]
    struct CssStreamingParser {
        state: CssStreamState,
        parent_index: usize,
    }

    enum CssStreamingBodyItem {
        Declaration,
        Ignored,
    }

    impl<'i> DeclarationParser<'i> for CssStreamingParser {
        type Declaration = CssStreamingBodyItem;
        type Error = ();

        fn parse_value<'t>(
            &mut self,
            name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
            _declaration_start: &ParserState,
        ) -> Result<Self::Declaration, ParseError<'i, Self::Error>> {
            let (value, important) = consume_declaration_value(input);
            self.state.declaration(&name, value, important);
            Ok(CssStreamingBodyItem::Declaration)
        }
    }

    impl<'i> AtRuleParser<'i> for CssStreamingParser {
        type Prelude = CssStreamPrelude<'i>;
        type AtRule = ();
        type Error = ();

        fn parse_prelude<'t>(
            &mut self,
            name: CowRcStr<'i>,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::Prelude, ParseError<'i, Self::Error>> {
            Ok(CssStreamPrelude {
                name,
                prelude: consume_raw(input),
            })
        }

        fn rule_without_block(
            &mut self,
            prelude: Self::Prelude,
            _start: &ParserState,
        ) -> Result<Self::AtRule, ()> {
            let index = self.state.begin_rule(
                CSS_RULE_AT,
                false,
                self.parent_index,
                &prelude.name,
                prelude.prelude,
            );
            self.state.end_rule(index, 0);
            Ok(())
        }

        fn parse_block<'t>(
            &mut self,
            prelude: Self::Prelude,
            _start: &ParserState,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::AtRule, ParseError<'i, Self::Error>> {
            let index = self.state.begin_rule(
                CSS_RULE_AT,
                true,
                self.parent_index,
                &prelude.name,
                prelude.prelude,
            );
            let mut declarations = 0usize;
            if prelude.name.eq_ignore_ascii_case("font-face")
                || prelude.name.eq_ignore_ascii_case("page")
            {
                let before = self.state.declaration_count.get();
                parse_css_stream_declaration_list(
                    input,
                    CssStreamingParser {
                        state: self.state.clone(),
                        parent_index: index,
                    },
                );
                declarations = (self.state.declaration_count.get() - before) as usize;
            } else if prelude.name.eq_ignore_ascii_case("media")
                || prelude.name.eq_ignore_ascii_case("supports")
                || prelude.name.eq_ignore_ascii_case("layer")
                || prelude.name.eq_ignore_ascii_case("container")
                || prelude.name.eq_ignore_ascii_case("keyframes")
                || prelude.name.eq_ignore_ascii_case("-webkit-keyframes")
            {
                parse_css_stream_rule_list(
                    input,
                    CssStreamingParser {
                        state: self.state.clone(),
                        parent_index: index,
                    },
                );
            } else {
                let _ = consume_raw(input);
            }
            self.state.end_rule(index, declarations);
            Ok(())
        }
    }

    impl<'i> QualifiedRuleParser<'i> for CssStreamingParser {
        type Prelude = &'i str;
        type QualifiedRule = ();
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
            let index = self
                .state
                .begin_rule(CSS_RULE_STYLE, true, self.parent_index, "", prelude);
            let before = self.state.declaration_count.get();
            parse_css_stream_declaration_list(
                input,
                CssStreamingParser {
                    state: self.state.clone(),
                    parent_index: index,
                },
            );
            self.state.end_rule(
                index,
                (self.state.declaration_count.get() - before) as usize,
            );
            Ok(())
        }
    }

    struct CssStreamingDeclarationListParser {
        parser: CssStreamingParser,
    }

    impl<'i> DeclarationParser<'i> for CssStreamingDeclarationListParser {
        type Declaration = CssStreamingBodyItem;
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

    impl<'i> AtRuleParser<'i> for CssStreamingDeclarationListParser {
        type Prelude = ();
        type AtRule = CssStreamingBodyItem;
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
            Ok(CssStreamingBodyItem::Ignored)
        }

        fn parse_block<'t>(
            &mut self,
            _prelude: Self::Prelude,
            _start: &ParserState,
            input: &mut Parser<'i, 't>,
        ) -> Result<Self::AtRule, ParseError<'i, Self::Error>> {
            let _ = consume_raw(input);
            Ok(CssStreamingBodyItem::Ignored)
        }
    }

    impl<'i> QualifiedRuleParser<'i> for CssStreamingDeclarationListParser {
        type Prelude = ();
        type QualifiedRule = CssStreamingBodyItem;
        type Error = ();
    }

    impl RuleBodyItemParser<'_, CssStreamingBodyItem, ()> for CssStreamingDeclarationListParser {
        fn parse_declarations(&self) -> bool {
            true
        }

        fn parse_qualified(&self) -> bool {
            false
        }
    }

    fn parse_css_stream_declaration_list<'i>(
        input: &mut Parser<'i, '_>,
        parser: CssStreamingParser,
    ) {
        let errors = parser.state.errors.clone();
        let mut body_parser = CssStreamingDeclarationListParser { parser };
        for item in RuleBodyParser::new(input, &mut body_parser) {
            if item.is_err() {
                errors.set(errors.get() + 1);
            }
        }
    }

    fn parse_css_stream_rule_list<'i>(input: &mut Parser<'i, '_>, mut parser: CssStreamingParser) {
        let errors = parser.state.errors.clone();
        for item in StyleSheetParser::new(input, &mut parser) {
            if item.is_err() {
                errors.set(errors.get() + 1);
            }
        }
    }

    fn css_stream_error(status: u32) -> CssStreamResult {
        CssStreamResult {
            status,
            ..Default::default()
        }
    }

    unsafe fn css_stream_state(
        callbacks: *const CssSinkVTable,
        context: *mut c_void,
    ) -> Result<CssStreamState, CssStreamResult> {
        let Some(callbacks) = (unsafe { callbacks.as_ref() }).copied() else {
            return Err(css_stream_error(STATUS_INVALID_ARGUMENT));
        };
        if context.is_null()
            || callbacks.begin_rule.is_none()
            || callbacks.declaration.is_none()
            || callbacks.end_rule.is_none()
        {
            return Err(css_stream_error(STATUS_INVALID_ARGUMENT));
        }
        Ok(CssStreamState {
            callbacks,
            context,
            errors: std::rc::Rc::new(Cell::new(0)),
            callback_failed: std::rc::Rc::new(Cell::new(false)),
            rule_count: std::rc::Rc::new(Cell::new(0)),
            declaration_count: std::rc::Rc::new(Cell::new(0)),
        })
    }

    fn finish_css_stream(state: CssStreamState) -> CssStreamResult {
        CssStreamResult {
            status: if state.callback_failed.get() {
                STATUS_CALLBACK_FAILED
            } else {
                STATUS_OK
            },
            parse_error_count: state.errors.get(),
            rule_count: state.rule_count.get(),
            declaration_count: state.declaration_count.get(),
            rust_allocation_count: ALLOCATION_COUNT.with(Cell::get),
            rust_peak_bytes: ALLOCATION_PEAK.with(Cell::get),
            rust_retained_bytes: ALLOCATION_CURRENT.with(Cell::get),
        }
    }

    #[no_mangle]
    pub extern "C" fn webscene_css_stream_abi_version() -> u32 {
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_css_stream_stylesheet(
        input: ByteSlice,
        callbacks: *const CssSinkVTable,
        context: *mut c_void,
    ) -> CssStreamResult {
        let input = match css_input(input) {
            Some(input) => input,
            None => return css_stream_error(STATUS_INVALID_ARGUMENT),
        };
        let state = match unsafe { css_stream_state(callbacks, context) } {
            Ok(state) => state,
            Err(error) => return error,
        };
        reset_allocation_metrics();
        catch_unwind(AssertUnwindSafe(|| {
            let mut parser_input = ParserInput::new(input);
            let mut parser_input = Parser::new(&mut parser_input);
            parse_css_stream_rule_list(
                &mut parser_input,
                CssStreamingParser {
                    state: state.clone(),
                    parent_index: CSS_NO_PARENT,
                },
            );
            finish_css_stream(state)
        }))
        .unwrap_or_else(|_| css_stream_error(STATUS_PANIC))
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_css_stream_declarations(
        input: ByteSlice,
        callbacks: *const CssSinkVTable,
        context: *mut c_void,
    ) -> CssStreamResult {
        let input = match css_input(input) {
            Some(input) => input,
            None => return css_stream_error(STATUS_INVALID_ARGUMENT),
        };
        let state = match unsafe { css_stream_state(callbacks, context) } {
            Ok(state) => state,
            Err(error) => return error,
        };
        reset_allocation_metrics();
        catch_unwind(AssertUnwindSafe(|| {
            let mut parser_input = ParserInput::new(input);
            let mut parser_input = Parser::new(&mut parser_input);
            parse_css_stream_declaration_list(
                &mut parser_input,
                CssStreamingParser {
                    state: state.clone(),
                    parent_index: CSS_NO_PARENT,
                },
            );
            finish_css_stream(state)
        }))
        .unwrap_or_else(|_| css_stream_error(STATUS_PANIC))
    }
}

#[cfg(feature = "selectors")]
mod selector_syntax {
    use super::*;
    use cssparser::{
        serialize_identifier, CowRcStr, CssStringWriter, Parser as CssParser, ParserInput,
        SourceLocation, ToCss,
    };
    use precomputed_hash::PrecomputedHash;
    use selectors::parser::{
        Combinator, NonTSPseudoClass, ParseRelative, PseudoElement, SelectorParseError,
        SelectorParseErrorKind,
    };
    use selectors::{Parser as SelectorParser, SelectorImpl, SelectorList};
    use std::borrow::Borrow;
    use std::fmt::{self, Write};

    #[derive(Clone, Debug, Default, Eq, Hash, PartialEq)]
    struct Atom(String);

    impl Borrow<str> for Atom {
        fn borrow(&self) -> &str {
            &self.0
        }
    }

    impl From<String> for Atom {
        fn from(value: String) -> Self {
            Self(value)
        }
    }

    impl From<&str> for Atom {
        fn from(value: &str) -> Self {
            Self(value.to_owned())
        }
    }

    impl PrecomputedHash for Atom {
        fn precomputed_hash(&self) -> u32 {
            self.0.bytes().fold(2_166_136_261_u32, |hash, byte| {
                (hash ^ u32::from(byte)).wrapping_mul(16_777_619)
            })
        }
    }

    impl ToCss for Atom {
        fn to_css<W>(&self, destination: &mut W) -> fmt::Result
        where
            W: fmt::Write,
        {
            serialize_identifier(&self.0, destination)
        }
    }

    #[derive(Clone, Debug, Default, Eq, PartialEq)]
    struct AttributeValue(String);

    impl From<&str> for AttributeValue {
        fn from(value: &str) -> Self {
            Self(value.to_owned())
        }
    }

    impl ToCss for AttributeValue {
        fn to_css<W>(&self, destination: &mut W) -> fmt::Result
        where
            W: fmt::Write,
        {
            destination.write_char('"')?;
            write!(CssStringWriter::new(destination), "{}", self.0)?;
            destination.write_char('"')
        }
    }

    #[derive(Clone, Debug, Eq, PartialEq)]
    enum WebScenePseudoClass {
        Named(String),
        Functional(String, String),
    }

    impl ToCss for WebScenePseudoClass {
        fn to_css<W>(&self, destination: &mut W) -> fmt::Result
        where
            W: fmt::Write,
        {
            match self {
                Self::Named(name) => write!(destination, ":{name}"),
                Self::Functional(name, argument) => {
                    write!(destination, ":{name}({argument})")
                }
            }
        }
    }

    impl NonTSPseudoClass for WebScenePseudoClass {
        type Impl = WebSceneSelectorImpl;

        fn is_active_or_hover(&self) -> bool {
            matches!(self, Self::Named(name) if name == "active" || name == "hover")
        }

        fn is_user_action_state(&self) -> bool {
            matches!(
                self,
                Self::Named(name)
                    if name == "active" || name == "hover" || name == "focus"
                        || name == "focus-visible" || name == "focus-within"
            )
        }
    }

    #[derive(Clone, Debug, Eq, PartialEq)]
    struct WebScenePseudoElement(String);

    impl ToCss for WebScenePseudoElement {
        fn to_css<W>(&self, destination: &mut W) -> fmt::Result
        where
            W: fmt::Write,
        {
            write!(destination, "::{}", self.0)
        }
    }

    impl PseudoElement for WebScenePseudoElement {
        type Impl = WebSceneSelectorImpl;

        fn is_before_or_after(&self) -> bool {
            self.0 == "before" || self.0 == "after"
        }
    }

    #[derive(Clone, Debug, PartialEq)]
    struct WebSceneSelectorImpl;

    impl SelectorImpl for WebSceneSelectorImpl {
        type ExtraMatchingData<'a> = std::marker::PhantomData<&'a ()>;
        type AttrValue = AttributeValue;
        type Identifier = Atom;
        type LocalName = Atom;
        type NamespaceUrl = Atom;
        type NamespacePrefix = Atom;
        type BorrowedLocalName = str;
        type BorrowedNamespaceUrl = str;
        type NonTSPseudoClass = WebScenePseudoClass;
        type PseudoElement = WebScenePseudoElement;
    }

    #[derive(Default)]
    struct WebSceneSelectorParser;

    fn is_supported_pseudo_class(name: &str) -> bool {
        matches!(
            name,
            "hover"
                | "active"
                | "focus"
                | "focus-visible"
                | "focus-within"
                | "disabled"
                | "enabled"
                | "checked"
                | "indeterminate"
                | "default"
                | "required"
                | "optional"
                | "valid"
                | "invalid"
                | "in-range"
                | "out-of-range"
                | "read-only"
                | "read-write"
                | "placeholder-shown"
                | "autofill"
                | "link"
                | "visited"
                | "any-link"
                | "local-link"
                | "target"
                | "target-within"
                | "defined"
                | "fullscreen"
                | "modal"
                | "open"
                | "picture-in-picture"
                | "user-valid"
                | "user-invalid"
                | "blank"
        )
    }

    fn is_supported_pseudo_element(name: &str) -> bool {
        matches!(
            name,
            "before"
                | "after"
                | "first-letter"
                | "first-line"
                | "selection"
                | "marker"
                | "placeholder"
                | "backdrop"
                | "file-selector-button"
                | "cue"
                | "cue-region"
                | "grammar-error"
                | "spelling-error"
                | "target-text"
                | "-webkit-scrollbar"
                | "-webkit-scrollbar-thumb"
                | "-webkit-scrollbar-track"
        )
    }

    impl<'i> SelectorParser<'i> for WebSceneSelectorParser {
        type Impl = WebSceneSelectorImpl;
        type Error = SelectorParseErrorKind<'i>;

        fn parse_nth_child_of(&self) -> bool {
            true
        }

        fn parse_is_and_where(&self) -> bool {
            true
        }

        fn parse_has(&self) -> bool {
            true
        }

        fn parse_non_ts_pseudo_class(
            &self,
            location: SourceLocation,
            name: CowRcStr<'i>,
        ) -> Result<WebScenePseudoClass, SelectorParseError<'i>> {
            let name = name.to_ascii_lowercase();
            if is_supported_pseudo_class(&name) {
                return Ok(WebScenePseudoClass::Named(name));
            }
            Err(
                location.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                    name.into(),
                )),
            )
        }

        fn parse_non_ts_functional_pseudo_class<'t>(
            &self,
            name: CowRcStr<'i>,
            parser: &mut CssParser<'i, 't>,
            _after_part: bool,
        ) -> Result<WebScenePseudoClass, SelectorParseError<'i>> {
            let name = name.to_ascii_lowercase();
            if name != "lang" && name != "dir" {
                return Err(parser.new_custom_error(
                    SelectorParseErrorKind::UnsupportedPseudoClassOrElement(name.into()),
                ));
            }
            let start = parser.position();
            while parser.next_including_whitespace_and_comments().is_ok() {}
            let argument = parser.slice_from(start).trim().to_owned();
            if argument.is_empty() {
                return Err(parser.new_custom_error(
                    SelectorParseErrorKind::UnsupportedPseudoClassOrElement(name.into()),
                ));
            }
            Ok(WebScenePseudoClass::Functional(name, argument))
        }

        fn parse_pseudo_element(
            &self,
            location: SourceLocation,
            name: CowRcStr<'i>,
        ) -> Result<WebScenePseudoElement, SelectorParseError<'i>> {
            let name = name.to_ascii_lowercase();
            if is_supported_pseudo_element(&name) {
                return Ok(WebScenePseudoElement(name));
            }
            Err(
                location.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                    name.into(),
                )),
            )
        }
    }

    #[derive(Default)]
    struct FlatSelector {
        serialized: String,
        specificity: u32,
        compounds: Vec<String>,
        combinators: Vec<u8>,
    }

    #[derive(Default)]
    struct SelectorOutput {
        selectors: Vec<FlatSelector>,
    }

    fn native_specificity(servo_specificity: u32) -> u32 {
        let ids = (servo_specificity >> 20).min(0xff);
        let classes = ((servo_specificity >> 10) & 0x3ff).min(0xff);
        let elements = (servo_specificity & 0x3ff).min(0xff);
        ids << 16 | classes << 8 | elements
    }

    fn flatten_selector(
        selector: &selectors::parser::Selector<WebSceneSelectorImpl>,
    ) -> FlatSelector {
        let components = selector.iter_raw_match_order().as_slice();
        let mut combinators = components
            .iter()
            .rev()
            .filter_map(|component| component.as_combinator());
        let compound_groups = components
            .split(|component| component.is_combinator())
            .rev();
        let mut compounds = Vec::new();
        let mut native_combinators = Vec::new();
        let mut current = String::new();
        for compound in compound_groups {
            for component in compound {
                let _ = component.to_css(&mut current);
            }
            match combinators.next() {
                Some(Combinator::Child) => {
                    compounds.push(std::mem::take(&mut current));
                    native_combinators.push(b'>');
                }
                Some(Combinator::Descendant) => {
                    compounds.push(std::mem::take(&mut current));
                    native_combinators.push(b' ');
                }
                Some(Combinator::NextSibling) => {
                    compounds.push(std::mem::take(&mut current));
                    native_combinators.push(b'+');
                }
                Some(Combinator::LaterSibling) => {
                    compounds.push(std::mem::take(&mut current));
                    native_combinators.push(b'~');
                }
                Some(Combinator::PseudoElement | Combinator::SlotAssignment | Combinator::Part) => {
                    // These are internal Servo edges and serialize without a CSS combinator.
                }
                None => compounds.push(std::mem::take(&mut current)),
            }
        }
        FlatSelector {
            serialized: selector.to_css_string(),
            specificity: native_specificity(selector.specificity()),
            compounds,
            combinators: native_combinators,
        }
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct SelectorParseResult {
        pub status: u32,
        pub selector_count: u64,
        pub rust_allocation_count: u64,
        pub rust_peak_bytes: u64,
        pub rust_retained_bytes: u64,
        pub handle: *mut c_void,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    pub struct SelectorView {
        pub serialized: ByteSlice,
        pub specificity: u32,
        pub compound_count: usize,
        pub combinator_count: usize,
    }

    fn selector_error(status: u32) -> SelectorParseResult {
        SelectorParseResult {
            status,
            ..Default::default()
        }
    }

    #[no_mangle]
    pub extern "C" fn webscene_selector_parser_abi_version() -> u32 {
        ABI_VERSION
    }

    #[no_mangle]
    pub extern "C" fn webscene_selector_parse(input: ByteSlice) -> SelectorParseResult {
        let Some(input) = read_slice(input).and_then(|bytes| std::str::from_utf8(bytes).ok())
        else {
            return selector_error(STATUS_INVALID_ARGUMENT);
        };
        reset_allocation_metrics();
        let parsed = catch_unwind(AssertUnwindSafe(|| {
            let mut parser_input = ParserInput::new(input);
            let mut parser = CssParser::new(&mut parser_input);
            let list = SelectorList::parse(&WebSceneSelectorParser, &mut parser, ParseRelative::No)
                .map_err(|_| ())?;
            parser.expect_exhausted().map_err(|_| ())?;
            Ok::<_, ()>(SelectorOutput {
                selectors: list.slice().iter().map(flatten_selector).collect(),
            })
        }));
        let output = match parsed {
            Ok(Ok(output)) if !output.selectors.is_empty() => output,
            Ok(Ok(_)) | Ok(Err(_)) => return selector_error(STATUS_INVALID_ARGUMENT),
            Err(_) => return selector_error(STATUS_PANIC),
        };
        let selector_count = output.selectors.len() as u64;
        let handle = Box::into_raw(Box::new(output)).cast::<c_void>();
        SelectorParseResult {
            status: STATUS_OK,
            selector_count,
            rust_allocation_count: ALLOCATION_COUNT.with(Cell::get),
            rust_peak_bytes: ALLOCATION_PEAK.with(Cell::get),
            rust_retained_bytes: ALLOCATION_CURRENT.with(Cell::get),
            handle,
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_selector_at(
        handle: *const c_void,
        index: usize,
        view: *mut SelectorView,
    ) -> u8 {
        let Some(output) = (unsafe { handle.cast::<SelectorOutput>().as_ref() }) else {
            return 0;
        };
        let Some(selector) = output.selectors.get(index) else {
            return 0;
        };
        let Some(view) = (unsafe { view.as_mut() }) else {
            return 0;
        };
        *view = SelectorView {
            serialized: ByteSlice::from_bytes(selector.serialized.as_bytes()),
            specificity: selector.specificity,
            compound_count: selector.compounds.len(),
            combinator_count: selector.combinators.len(),
        };
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_selector_compound_at(
        handle: *const c_void,
        selector_index: usize,
        compound_index: usize,
        value: *mut ByteSlice,
    ) -> u8 {
        let Some(output) = (unsafe { handle.cast::<SelectorOutput>().as_ref() }) else {
            return 0;
        };
        let Some(compound) = output
            .selectors
            .get(selector_index)
            .and_then(|selector| selector.compounds.get(compound_index))
        else {
            return 0;
        };
        let Some(value) = (unsafe { value.as_mut() }) else {
            return 0;
        };
        *value = ByteSlice::from_bytes(compound.as_bytes());
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_selector_combinator_at(
        handle: *const c_void,
        selector_index: usize,
        combinator_index: usize,
        value: *mut u8,
    ) -> u8 {
        let Some(output) = (unsafe { handle.cast::<SelectorOutput>().as_ref() }) else {
            return 0;
        };
        let Some(combinator) = output
            .selectors
            .get(selector_index)
            .and_then(|selector| selector.combinators.get(combinator_index))
        else {
            return 0;
        };
        let Some(value) = (unsafe { value.as_mut() }) else {
            return 0;
        };
        *value = *combinator;
        1
    }

    #[no_mangle]
    pub unsafe extern "C" fn webscene_selector_free(handle: *mut c_void) {
        if !handle.is_null() {
            drop(unsafe { Box::from_raw(handle.cast::<SelectorOutput>()) });
        }
    }
}
