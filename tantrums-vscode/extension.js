const vscode = require('vscode');

/* ═══════════════════════════════════════════════════════════
 *  TANTRUMS LANGUAGE EXTENSION
 *  Full language support: diagnostics, hover, completion,
 *  commands, type checking, and more.
 * ═══════════════════════════════════════════════════════════ */

let diagnosticCollection;

function activate(context) {
    diagnosticCollection = vscode.languages.createDiagnosticCollection('tantrums');
    context.subscriptions.push(diagnosticCollection);

    // ── Diagnostics (lint on open/save/edit) ──
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => lint(doc)),
        vscode.workspace.onDidSaveTextDocument(doc => lint(doc)),
        vscode.workspace.onDidChangeTextDocument(e => lint(e.document))
    );
    vscode.workspace.textDocuments.forEach(doc => lint(doc));

    // ── Hover Provider ──
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('tantrums', { provideHover })
    );

    // ── Completion Provider ──
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('tantrums', { provideCompletionItems }, '.', '(')
    );

    // ── Commands ──
    context.subscriptions.push(
        vscode.commands.registerCommand('tantrums.runFile', cmdRunFile),
        vscode.commands.registerCommand('tantrums.compileFile', cmdCompileFile),
        vscode.commands.registerCommand('tantrums.execBytecode', cmdExecBytecode)
    );
}

function deactivate() {
    if (diagnosticCollection) diagnosticCollection.dispose();
}


/* ═══════════════════════════════════════════════════════════
 *  SECTION 1: DIAGNOSTICS (Error Highlighting)
 * ═══════════════════════════════════════════════════════════ */

function lint(document) {
    if (document.languageId !== 'tantrums') return;

    const text = document.getText();
    const lines = text.split(/\r?\n/);
    const diagnostics = [];

    lintBrackets(lines, diagnostics);
    lintStrings(lines, diagnostics);
    lintSemicolons(lines, diagnostics);
    lintConditions(lines, diagnostics);
    lintTypeChecking(lines, diagnostics);

    diagnosticCollection.set(document.uri, diagnostics);
}

function lintBrackets(lines, diagnostics) {
    const stack = [];
    let inString = false, inLineComment = false, inBlockComment = false;

    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        const line = lines[lineNum];
        inLineComment = false;

        for (let col = 0; col < line.length; col++) {
            const ch = line[col], next = line[col + 1] || '';

            if (inBlockComment) {
                if (ch === '*' && next === '/') { inBlockComment = false; col++; }
                continue;
            }
            if (inLineComment) continue;
            if (!inString && ch === '/' && next === '*') { inBlockComment = true; col++; continue; }
            if (!inString && ch === '/' && next === '/') { inLineComment = true; continue; }
            if (ch === '"' && !inString) { inString = true; continue; }
            if (inString) {
                if (ch === '\\') { col++; continue; }
                if (ch === '"') inString = false;
                continue;
            }

            if (ch === '(' || ch === '[' || ch === '{') {
                stack.push({ char: ch, line: lineNum, col });
            }
            if (ch === ')' || ch === ']' || ch === '}') {
                const expected = ch === ')' ? '(' : ch === ']' ? '[' : '{';
                if (stack.length === 0) {
                    diagnostics.push(diag(lineNum, col, col + 1,
                        `Unexpected '${ch}' — no matching opening bracket.`, vscode.DiagnosticSeverity.Error));
                } else {
                    const top = stack[stack.length - 1];
                    if (top.char !== expected) {
                        diagnostics.push(diag(lineNum, col, col + 1,
                            `Mismatched '${ch}' — expected closing for '${top.char}' at line ${top.line + 1}.`, vscode.DiagnosticSeverity.Error));
                    }
                    stack.pop();
                }
            }
        }
    }

    if (inBlockComment) {
        diagnostics.push(diag(lines.length - 1, 0, 1, 'Unterminated block comment.', vscode.DiagnosticSeverity.Error));
    }
    for (const item of stack) {
        diagnostics.push(diag(item.line, item.col, item.col + 1,
            `Unclosed '${item.char}' — no matching closing bracket.`, vscode.DiagnosticSeverity.Error));
    }
}

function lintStrings(lines, diagnostics) {
    let inString = false, inBlockComment = false, stringStart = { line: 0, col: 0 };

    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        const line = lines[lineNum];
        let inLineComment = false;

        for (let col = 0; col < line.length; col++) {
            const ch = line[col], next = line[col + 1] || '';

            if (inBlockComment) {
                if (ch === '*' && next === '/') { inBlockComment = false; col++; }
                continue;
            }
            if (inLineComment) continue;
            if (!inString && ch === '/' && next === '*') { inBlockComment = true; col++; continue; }
            if (!inString && ch === '/' && next === '/') { inLineComment = true; continue; }

            if (ch === '"' && !inString) {
                inString = true;
                stringStart = { line: lineNum, col };
                continue;
            }
            if (inString) {
                if (ch === '\\') { col++; continue; }
                if (ch === '"') { inString = false; }
            }
        }

        if (inString) {
            diagnostics.push(diag(stringStart.line, stringStart.col, line.length,
                'Unterminated string literal.', vscode.DiagnosticSeverity.Error));
            inString = false;
        }
    }
}

function lintSemicolons(lines, diagnostics) {
    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        const stripped = stripCommentsAndStrings(lines[lineNum]);
        const trimmed = stripped.trim();
        if (trimmed === '') continue;

        if (needsSemicolon(trimmed) && !trimmed.endsWith(';') &&
            !trimmed.endsWith('{') && !trimmed.endsWith('}')) {
            diagnostics.push(diag(lineNum, lines[lineNum].length, lines[lineNum].length,
                'Missing semicolon at end of statement.', vscode.DiagnosticSeverity.Warning));
        }
    }
}

function lintConditions(lines, diagnostics) {
    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        const stripped = stripCommentsAndStrings(lines[lineNum]);
        const trimmed = stripped.trim();

        // Empty condition: if() / while()
        const emptyMatch = lines[lineNum].match(/\b(if|while)\s*\(\s*\)/);
        if (emptyMatch) {
            const start = lines[lineNum].indexOf(emptyMatch[0]);
            diagnostics.push(diag(lineNum, start, start + emptyMatch[0].length,
                `Empty condition in '${emptyMatch[1]}' statement.`, vscode.DiagnosticSeverity.Error));
        }

        // = instead of == in conditions
        const condMatch = trimmed.match(/\b(?:if|while)\s*\((.+)\)/);
        if (condMatch) {
            const cond = condMatch[1];
            if (/(?<![!=<>])=(?!=|>|<)/.test(cond) && !/==/.test(cond)) {
                diagnostics.push(diag(lineNum, 0, lines[lineNum].length,
                    "Possible mistake: '=' in condition — did you mean '=='?", vscode.DiagnosticSeverity.Warning));
            }
        }
    }
}

function lintTypeChecking(lines, diagnostics) {
    // Collect function signatures
    const sigs = {};
    const funcRe = /\btantrum\s+(?:(int|float|string|bool|list|map)\s+)?(\w+)\s*\(([^)]*)\)/;
    for (let i = 0; i < lines.length; i++) {
        const m = lines[i].match(funcRe);
        if (!m) continue;
        const params = [];
        if (m[3].trim()) {
            for (const p of m[3].split(',')) {
                const parts = p.trim().split(/\s+/);
                if (parts.length >= 2 && ['int', 'float', 'string', 'bool', 'list', 'map'].includes(parts[0])) {
                    params.push({ type: parts[0], name: parts[1] });
                } else {
                    params.push({ type: null, name: parts[parts.length - 1] });
                }
            }
        }
        sigs[m[2]] = { params, retType: m[1] || null };
    }

    // Check call sites
    const callRe = /\b(\w+)\s*\(([^)]*)\)/g;
    const skip = new Set(['print', 'input', 'len', 'range', 'type', 'append', 'tantrum', 'if', 'while', 'for']);
    for (let i = 0; i < lines.length; i++) {
        const stripped = stripCommentsAndStrings(lines[i]);
        let cm;
        while ((cm = callRe.exec(stripped)) !== null) {
            if (skip.has(cm[1])) continue;
            const sig = sigs[cm[1]];
            if (!sig) continue;
            const origCall = lines[i].match(new RegExp(`\\b${cm[1]}\\s*\\(([^)]*)\\)`));
            if (!origCall) continue;
            const argStrs = splitArgs(origCall[1]);
            for (let j = 0; j < argStrs.length && j < sig.params.length; j++) {
                const pt = sig.params[j].type;
                if (!pt) continue;
                const at = inferLiteralType(argStrs[j].trim());
                if (!at) continue;
                if (!typesOk(pt, at)) {
                    const col = lines[i].indexOf(origCall[0]);
                    diagnostics.push(diag(i, col, col + origCall[0].length,
                        `Function '${cm[1]}' param ${j + 1} expects '${pt}' but got '${at}'.`,
                        vscode.DiagnosticSeverity.Error));
                }
            }
        }
        callRe.lastIndex = 0;
    }
}


/* ═══════════════════════════════════════════════════════════
 *  SECTION 2: HOVER PROVIDER (Documentation on Hover)
 * ═══════════════════════════════════════════════════════════ */

const HOVER_DOCS = {
    // Built-in functions
    'print': { sig: 'print(value)', desc: 'Prints any value to the console, followed by a newline.' },
    'input': { sig: 'input(prompt?)', desc: 'Reads a line of text from stdin. When assigned to a typed variable (int, float, bool), the input is automatically cast to that type.' },
    'len': { sig: 'len(value) → int', desc: 'Returns the length of a string, list, or map.' },
    'range': { sig: 'range(n) → list', desc: 'Generates a list of integers from 0 to n-1. Used with for-in loops.' },
    'type': { sig: 'type(value) → string', desc: 'Returns the type name of a value as a string: "int", "float", "string", "bool", "list", "map", "null".' },
    'append': { sig: 'append(list, value)', desc: 'Appends a value to the end of a list. Modifies the list in-place.' },

    // Keywords
    'tantrum': { sig: 'tantrum [type] name(params) { ... }', desc: '**Function declaration keyword.** Defines a new function. Optional return type comes before the name.' },
    'if': { sig: 'if (condition) { ... }', desc: '**Conditional statement.** Executes the block if the condition is truthy.' },
    'else': { sig: 'else { ... }', desc: '**Else clause.** Executes when the preceding if condition was falsy.' },
    'while': { sig: 'while (condition) { ... }', desc: '**While loop.** Repeats the block as long as the condition is truthy.' },
    'for': { sig: 'for var in iterable { ... }', desc: '**For-in loop.** Iterates over a list, string, or range.' },
    'in': { sig: 'for var in iterable', desc: '**In keyword.** Used in for-in loops to specify the iterable.' },
    'return': { sig: 'return value;', desc: '**Return statement.** Returns a value from the current function.' },
    'throw': { sig: 'throw "message";', desc: '**Throw statement.** Halts execution with an error message.' },
    'alloc': { sig: 'alloc type(value)', desc: '**Manual allocation.** Creates a heap-allocated pointer. Must be freed with `free`.' },
    'free': { sig: 'free(pointer);', desc: '**Free memory.** Deallocates a manually allocated pointer.' },
    'use': { sig: 'use filename.42AHH;', desc: '**Import statement.** Imports all functions and globals from another .42AHH file in the same directory.' },
    'true': { sig: 'true', desc: '**Boolean literal.** Represents the boolean value true.' },
    'false': { sig: 'false', desc: '**Boolean literal.** Represents the boolean value false.' },
    'null': { sig: 'null', desc: '**Null literal.** Represents the absence of a value.' },
    'and': { sig: 'expr and expr', desc: '**Logical AND.** Short-circuit: returns false if left is falsy, otherwise returns right.' },
    'or': { sig: 'expr or expr', desc: '**Logical OR.** Short-circuit: returns left if truthy, otherwise returns right.' },

    // Types
    'int': { sig: 'int', desc: '**Integer type.** 64-bit signed integer. Example: `int x = 42;`' },
    'float': { sig: 'float', desc: '**Float type.** 64-bit double precision. Example: `float pi = 3.14;`' },
    'string': { sig: 'string', desc: '**String type.** Immutable UTF-8 string. Supports escape sequences: \\n \\t \\\\ \\"' },
    'bool': { sig: 'bool', desc: '**Boolean type.** Either `true` or `false`. Example: `bool flag = true;`' },
    'list': { sig: 'list', desc: '**List type.** Mutable ordered collection. Example: `list items = [1, 2, 3];`' },
    'map': { sig: 'map', desc: '**Map type.** Key-value pairs with string keys. Example: `map data = {"name": "Tantrums"};`' },
};

function provideHover(document, position) {
    const range = document.getWordRangeAtPosition(position);
    if (!range) return null;
    const word = document.getText(range);
    const info = HOVER_DOCS[word];
    if (!info) return null;

    const md = new vscode.MarkdownString();
    md.appendCodeblock(info.sig, 'tantrums');
    md.appendMarkdown('\n\n' + info.desc);
    return new vscode.Hover(md, range);
}


/* ═══════════════════════════════════════════════════════════
 *  SECTION 3: COMPLETION PROVIDER (IntelliSense)
 * ═══════════════════════════════════════════════════════════ */

function provideCompletionItems(document, position) {
    const items = [];
    const lineText = document.lineAt(position).text;
    const prefix = lineText.substring(0, position.character);

    // Keywords
    const keywords = ['tantrum', 'if', 'else', 'while', 'for', 'in', 'return', 'throw', 'alloc', 'free', 'use', 'and', 'or', 'true', 'false', 'null'];
    for (const kw of keywords) {
        const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
        item.detail = 'Keyword';
        items.push(item);
    }

    // Types
    const types = ['int', 'float', 'string', 'bool', 'list', 'map'];
    for (const t of types) {
        const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.TypeParameter);
        item.detail = 'Type';
        const info = HOVER_DOCS[t];
        if (info) item.documentation = new vscode.MarkdownString(info.desc);
        items.push(item);
    }

    // Built-in functions with signatures
    const builtins = [
        { name: 'print', sig: 'print(value)', insert: 'print(${1:value})' },
        { name: 'input', sig: 'input(prompt?)', insert: 'input(${1:"prompt"})' },
        { name: 'len', sig: 'len(value) → int', insert: 'len(${1:value})' },
        { name: 'range', sig: 'range(n) → list', insert: 'range(${1:n})' },
        { name: 'type', sig: 'type(value) → string', insert: 'type(${1:value})' },
        { name: 'append', sig: 'append(list, value)', insert: 'append(${1:list}, ${2:value})' },
    ];
    for (const b of builtins) {
        const item = new vscode.CompletionItem(b.name, vscode.CompletionItemKind.Function);
        item.detail = b.sig;
        item.insertText = new vscode.SnippetString(b.insert);
        const info = HOVER_DOCS[b.name];
        if (info) item.documentation = new vscode.MarkdownString(info.desc);
        items.push(item);
    }

    // Collect user-defined functions from current document
    const text = document.getText();
    const funcRegex = /\btantrum\s+(?:(?:int|float|string|bool|list|map)\s+)?(\w+)\s*\(([^)]*)\)/g;
    let fm;
    while ((fm = funcRegex.exec(text)) !== null) {
        if (fm[1] === 'main') continue; // skip main
        const name = fm[1];
        const params = fm[2];
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
        item.detail = `tantrum ${name}(${params})`;
        // Build snippet with numbered params
        const paramParts = params.split(',').map(p => p.trim()).filter(p => p);
        const snippetParams = paramParts.map((p, i) => {
            const parts = p.split(/\s+/);
            const pname = parts[parts.length - 1];
            return `\${${i + 1}:${pname}}`;
        }).join(', ');
        item.insertText = new vscode.SnippetString(`${name}(${snippetParams})`);
        item.documentation = new vscode.MarkdownString(`User-defined function \`${name}\``);
        items.push(item);
    }

    // Collect variable names from current document
    const varRegex = /\b(?:int|float|string|bool|list|map)?\s*([a-zA-Z_]\w*)\s*=/g;
    const seenVars = new Set();
    let vm;
    while ((vm = varRegex.exec(text)) !== null) {
        const name = vm[1];
        if (seenVars.has(name) || keywords.includes(name) || types.includes(name)) continue;
        seenVars.add(name);
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Variable);
        item.detail = 'Variable';
        items.push(item);
    }

    return items;
}


/* ═══════════════════════════════════════════════════════════
 *  SECTION 4: COMMANDS (Run / Compile / Execute)
 * ═══════════════════════════════════════════════════════════ */

function cmdRunFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) { vscode.window.showErrorMessage('No file open.'); return; }
    const filePath = editor.document.fileName;
    if (!filePath.endsWith('.42AHH') && !filePath.endsWith('.42ahh')) {
        vscode.window.showErrorMessage('Not a .42AHH file.'); return;
    }
    // Save before running
    editor.document.save().then(() => {
        const terminal = getTerminal();
        terminal.show();
        terminal.sendText(`tantrums run "${filePath}"`);
    });
}

function cmdCompileFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) { vscode.window.showErrorMessage('No file open.'); return; }
    const filePath = editor.document.fileName;
    if (!filePath.endsWith('.42AHH') && !filePath.endsWith('.42ahh')) {
        vscode.window.showErrorMessage('Not a .42AHH file.'); return;
    }
    editor.document.save().then(() => {
        const terminal = getTerminal();
        terminal.show();
        terminal.sendText(`tantrums compile "${filePath}"`);
    });
}

function cmdExecBytecode() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) { vscode.window.showErrorMessage('No file open.'); return; }
    const filePath = editor.document.fileName;
    if (!filePath.endsWith('.42ass')) {
        vscode.window.showErrorMessage('Not a .42ass file.'); return;
    }
    const terminal = getTerminal();
    terminal.show();
    terminal.sendText(`tantrums exec "${filePath}"`);
}

function getTerminal() {
    const existing = vscode.window.terminals.find(t => t.name === 'Tantrums');
    if (existing) return existing;
    return vscode.window.createTerminal('Tantrums');
}


/* ═══════════════════════════════════════════════════════════
 *  SECTION 5: HELPERS
 * ═══════════════════════════════════════════════════════════ */

function needsSemicolon(trimmed) {
    if (trimmed === '') return false;
    if (trimmed.startsWith('//')) return false;
    if (trimmed.startsWith('/*') || trimmed.startsWith('*') || trimmed.endsWith('*/')) return false;
    if (trimmed === '{' || trimmed === '}') return false;
    if (/^\}?\s*else\b/.test(trimmed)) return false;
    if (/^(if|while|for)\s*[\(]/.test(trimmed)) return false;
    if (/^(if|while|for)\s+/.test(trimmed) && !trimmed.endsWith(';')) return false;
    if (/^tantrum\s+/.test(trimmed)) return false;
    if (trimmed.startsWith('}')) return false;
    if (/^use\s+/.test(trimmed)) return true; // use statements need semicolons
    return true;
}

function stripCommentsAndStrings(line) {
    let result = '', inStr = false;
    for (let i = 0; i < line.length; i++) {
        if (inStr) {
            if (line[i] === '\\') { i++; continue; }
            if (line[i] === '"') { inStr = false; result += '""'; }
            continue;
        }
        if (line[i] === '"') { inStr = true; continue; }
        if (line[i] === '/' && line[i + 1] === '/') break;
        if (line[i] === '/' && line[i + 1] === '*') {
            const end = line.indexOf('*/', i + 2);
            if (end !== -1) { i = end + 1; continue; }
            break;
        }
        result += line[i];
    }
    return result;
}

function splitArgs(str) {
    const args = []; let depth = 0, current = '';
    for (const ch of str) {
        if (ch === '(' || ch === '[') depth++;
        if (ch === ')' || ch === ']') depth--;
        if (ch === ',' && depth === 0) { args.push(current); current = ''; continue; }
        current += ch;
    }
    if (current.trim()) args.push(current);
    return args;
}

function inferLiteralType(expr) {
    if (/^"/.test(expr)) return 'string';
    if (/^(true|false)$/.test(expr)) return 'bool';
    if (/^null$/.test(expr)) return 'null';
    if (/^\d+\.\d+$/.test(expr)) return 'float';
    if (/^-?\d+$/.test(expr)) return 'int';
    if (/^\[/.test(expr)) return 'list';
    if (/^\{/.test(expr)) return 'map';
    return null;
}

function typesOk(expected, actual) {
    if (expected === actual) return true;
    if (expected === 'float' && actual === 'int') return true;
    return false;
}

function diag(line, startCol, endCol, message, severity) {
    return new vscode.Diagnostic(
        new vscode.Range(line, startCol, line, endCol),
        message, severity
    );
}

module.exports = { activate, deactivate };
