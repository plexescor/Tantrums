// Tantrums Language Extension - VS Code
// Comprehensive diagnostics, hover, completion, commands

const vscode = require('vscode');

var diagCollection;

// ============================================================
//  ACTIVATION
// ============================================================

function activate(context) {
    diagCollection = vscode.languages.createDiagnosticCollection('tantrums');
    context.subscriptions.push(diagCollection);

    if (vscode.window.activeTextEditor) {
        lint(vscode.window.activeTextEditor.document);
    }
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(function (doc) { lint(doc); }),
        vscode.workspace.onDidSaveTextDocument(function (doc) { lint(doc); }),
        vscode.workspace.onDidChangeTextDocument(function (e) { lint(e.document); })
    );

    context.subscriptions.push(
        vscode.languages.registerHoverProvider('tantrums', {
            provideHover: function (doc, pos) { return doHover(doc, pos); }
        })
    );

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('tantrums', {
            provideCompletionItems: function (doc, pos) { return doComplete(doc, pos); }
        }, '.', '(')
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('tantrums.runFile', function () { runCmd('run'); }),
        vscode.commands.registerCommand('tantrums.compileFile', function () { runCmd('compile'); }),
        vscode.commands.registerCommand('tantrums.execBytecode', function () { runCmd('exec'); })
    );
}

function deactivate() {
    if (diagCollection) { diagCollection.dispose(); }
}


// ============================================================
//  MASTER LINT
// ============================================================

function lint(document) {
    if (!document || document.languageId !== 'tantrums') { return; }

    var text = document.getText();
    var lines = text.split(/\r?\n/);
    var diags = [];

    // Pre-parse: collect function signatures and variable declarations
    var funcSigs = collectFunctions(lines);
    var varDecls = collectVariables(lines);

    // All checks
    checkModeDirective(lines, diags);
    checkBrackets(lines, diags);
    checkStrings(lines, diags);
    checkEscapeSequences(lines, diags);
    checkSemicolons(lines, diags);
    checkConditions(lines, diags);
    checkFunctionTypes(lines, funcSigs, diags);
    checkAssignmentTypes(lines, varDecls, diags);
    checkDuplicateFunctions(lines, diags);
    checkDuplicateVariables(lines, diags);
    checkUndefinedFunctions(lines, funcSigs, diags);
    checkUndefinedVariables(lines, funcSigs, varDecls, diags);
    checkReturnOutsideFunction(lines, diags);
    checkDeadCode(lines, diags);
    checkDivisionByZero(lines, diags);
    checkMissingReturn(lines, funcSigs, diags);
    checkUnusedVariables(lines, varDecls, diags);
    checkShadowBuiltins(lines, diags);
    checkEmptyBlocks(lines, diags);

    diagCollection.set(document.uri, diags);
}


// ============================================================
//  PRE-PARSE: collect function sigs and variable declarations
// ============================================================

var BUILTIN_FUNCS = { 'print': 1, 'input': 1, 'len': 1, 'range': 1, 'type': 1, 'append': 1 };
var KEYWORDS = ['tantrum', 'if', 'else', 'while', 'for', 'in', 'return', 'throw', 'alloc',
    'free', 'use', 'and', 'or', 'true', 'false', 'null', 'try', 'catch',
    'int', 'float', 'string', 'bool', 'list', 'map'];
var TYPES = ['int', 'float', 'string', 'bool', 'list', 'map'];

function collectFunctions(lines) {
    var sigs = {};
    var re = /\btantrum\s+(?:(int|float|string|bool|list|map)\s+)?(\w+)\s*\(([^)]*)\)/;
    for (var i = 0; i < lines.length; i++) {
        var m = lines[i].match(re);
        if (!m) { continue; }
        var params = [];
        if (m[3].trim()) {
            var parts = m[3].split(',');
            for (var p = 0; p < parts.length; p++) {
                var pp = parts[p].trim().split(/\s+/);
                if (pp.length >= 2 && TYPES.indexOf(pp[0]) >= 0) {
                    params.push({ type: pp[0], name: pp[1] });
                } else {
                    params.push({ type: null, name: pp[pp.length - 1] });
                }
            }
        }
        if (!sigs[m[2]]) {
            sigs[m[2]] = { params: params, ret: m[1] || null, line: i, count: 1 };
        } else {
            sigs[m[2]].count++;
        }
    }
    return sigs;
}

function collectVariables(lines) {
    var vars = {};
    var declRe = /\b(int|float|string|bool|list|map)\s+(\w+)\s*=/;
    var dynRe = /^(\s*)(\w+)\s*=\s*[^=]/;
    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]).trim();
        var m = stripped.match(declRe);
        if (m) {
            if (!vars[m[2]]) {
                vars[m[2]] = { type: m[1], line: i, typed: true };
            }
            continue;
        }
        // Dynamic assignments (skip if inside function decl, if/while/for)
        if (/^(if|while|for|tantrum|else|return|throw|use|try|catch|print|input|len|append|range|type|free|alloc)/.test(stripped)) { continue; }
        if (stripped === '{' || stripped === '}' || stripped === '') { continue; }
        var dm = stripped.match(dynRe);
        if (dm && dm[2] && KEYWORDS.indexOf(dm[2]) < 0) {
            if (!vars[dm[2]]) {
                vars[dm[2]] = { type: null, line: i, typed: false };
            }
        }
    }
    return vars;
}


// ============================================================
//  DIAGNOSTIC CHECKS
// ============================================================

// --- #mode directive ---
function checkModeDirective(lines, diags) {
    for (var i = 0; i < lines.length; i++) {
        var trimmed = lines[i].trim();
        if (trimmed.indexOf('#') === 0) {
            var match = trimmed.match(/^#mode\s+(static|dynamic|both)\s*;?\s*$/);
            if (!match) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "Invalid directive. Valid: #mode static; / #mode dynamic; / #mode both;",
                    vscode.DiagnosticSeverity.Error));
            } else if (trimmed.indexOf(';') === -1) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "Missing semicolon after #mode directive.",
                    vscode.DiagnosticSeverity.Warning));
            }
        }
    }
}

// --- brackets ---
function checkBrackets(lines, diags) {
    var stack = [];
    var inStr = false;
    var inBlock = false;

    for (var ln = 0; ln < lines.length; ln++) {
        var line = lines[ln];
        var inLine = false;

        for (var c = 0; c < line.length; c++) {
            var ch = line[c];
            var nx = (c + 1 < line.length) ? line[c + 1] : '';

            if (inBlock) {
                if (ch === '*' && nx === '/') { inBlock = false; c++; }
                continue;
            }
            if (inLine) { continue; }
            if (!inStr && ch === '/' && nx === '*') { inBlock = true; c++; continue; }
            if (!inStr && ch === '/' && nx === '/') { inLine = true; continue; }
            if (ch === '"' && !inStr) { inStr = true; continue; }
            if (inStr) {
                if (ch === '\\') { c++; continue; }
                if (ch === '"') { inStr = false; }
                continue;
            }

            if (ch === '(' || ch === '[' || ch === '{') {
                stack.push({ ch: ch, ln: ln, col: c });
            }
            if (ch === ')' || ch === ']' || ch === '}') {
                var exp = ch === ')' ? '(' : ch === ']' ? '[' : '{';
                if (stack.length === 0) {
                    diags.push(makeDiag(ln, c, c + 1,
                        "Unexpected '" + ch + "' — no matching opener.",
                        vscode.DiagnosticSeverity.Error));
                } else {
                    var top = stack[stack.length - 1];
                    if (top.ch !== exp) {
                        diags.push(makeDiag(ln, c, c + 1,
                            "Mismatched '" + ch + "' — expected closing for '" + top.ch + "' from line " + (top.ln + 1) + ".",
                            vscode.DiagnosticSeverity.Error));
                    }
                    stack.pop();
                }
            }
        }
    }

    for (var s = 0; s < stack.length; s++) {
        var item = stack[s];
        diags.push(makeDiag(item.ln, item.col, item.col + 1,
            "Unclosed '" + item.ch + "'.",
            vscode.DiagnosticSeverity.Error));
    }
}

// --- unterminated strings ---
function checkStrings(lines, diags) {
    var inStr = false;
    var inBlock = false;
    var strLine = 0;
    var strCol = 0;

    for (var ln = 0; ln < lines.length; ln++) {
        var line = lines[ln];
        var inLine = false;

        for (var c = 0; c < line.length; c++) {
            var ch = line[c];
            var nx = (c + 1 < line.length) ? line[c + 1] : '';

            if (inBlock) {
                if (ch === '*' && nx === '/') { inBlock = false; c++; }
                continue;
            }
            if (inLine) { continue; }
            if (!inStr && ch === '/' && nx === '*') { inBlock = true; c++; continue; }
            if (!inStr && ch === '/' && nx === '/') { inLine = true; continue; }

            if (ch === '"' && !inStr) {
                inStr = true; strLine = ln; strCol = c;
                continue;
            }
            if (inStr) {
                if (ch === '\\') { c++; continue; }
                if (ch === '"') { inStr = false; }
            }
        }

        if (inStr) {
            diags.push(makeDiag(strLine, strCol, line.length,
                "Unterminated string literal.",
                vscode.DiagnosticSeverity.Error));
            inStr = false;
        }
    }
}

// --- invalid escape sequences ---
function checkEscapeSequences(lines, diags) {
    var validEscapes = ['n', 't', '\\', '"', 'r', '0'];

    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]);
        var inStr = false;

        for (var c = 0; c < stripped.length; c++) {
            if (stripped[c] === '"') {
                inStr = !inStr;
                continue;
            }
            if (inStr && stripped[c] === '\\') {
                var next = (c + 1 < stripped.length) ? stripped[c + 1] : '';
                if (next && validEscapes.indexOf(next) < 0) {
                    var col = lines[i].indexOf('\\' + next, c > 0 ? c - 1 : 0);
                    if (col === -1) { col = c; }
                    diags.push(makeDiag(i, col, col + 2,
                        "Invalid escape sequence '\\" + next + "'. Valid: \\n \\t \\\\ \\\" \\r \\0",
                        vscode.DiagnosticSeverity.Error));
                }
                c++; // skip the escaped char
            }
        }
    }
}

// --- semicolons ---
function checkSemicolons(lines, diags) {
    for (var i = 0; i < lines.length; i++) {
        var raw = lines[i];
        var stripped = stripComments(raw);
        var trimmed = stripped.trim();
        if (trimmed === '' || trimmed === '{' || trimmed === '}') { continue; }
        if (trimmed.indexOf('//') === 0 || trimmed.indexOf('/*') === 0 || trimmed.indexOf('*') === 0) { continue; }
        if (/^\}?\s*else/.test(trimmed)) { continue; }
        if (/^(if|while|for)\s*[\(]/.test(trimmed) || /^(if|while|for)\s+/.test(trimmed)) { continue; }
        if (/^tantrum\s+/.test(trimmed)) { continue; }
        if (/^try\s*$/.test(trimmed) || /^catch/.test(trimmed)) { continue; }
        if (/^#mode/.test(trimmed)) { continue; }
        if (trimmed.indexOf('}') === 0) { continue; }

        if (needsSemi(trimmed) && !trimmed.endsWith(';') && !trimmed.endsWith('{') && !trimmed.endsWith('}')) {
            diags.push(makeDiag(i, raw.length, raw.length,
                "Missing ';' at end of statement.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

function needsSemi(t) {
    if (/^(return|throw|print|append|use|free)\b/.test(t)) { return true; }
    if (/^\w+\s*=/.test(t)) { return true; }
    if (/^(int|float|string|bool|list|map)\s+\w+/.test(t)) { return true; }
    if (/^\w+\s*\(/.test(t)) { return true; }
    if (/(\+\+|--)/.test(t)) { return true; }
    if (/(\+=|-=|\*=|\/=|%=)/.test(t)) { return true; }
    return false;
}

// --- empty conditions ---
function checkConditions(lines, diags) {
    for (var i = 0; i < lines.length; i++) {
        var m = lines[i].match(/\b(if|while)\s*\(\s*\)/);
        if (m) {
            var col = lines[i].indexOf(m[0]);
            diags.push(makeDiag(i, col, col + m[0].length,
                "Empty condition in '" + m[1] + "' statement.",
                vscode.DiagnosticSeverity.Error));
        }
    }
}

// --- function call type checking ---
function checkFunctionTypes(lines, funcSigs, diags) {
    var skip = {
        'print': 1, 'input': 1, 'len': 1, 'range': 1, 'type': 1, 'append': 1,
        'tantrum': 1, 'if': 1, 'while': 1, 'for': 1, 'catch': 1
    };
    var callRe = /\b(\w+)\s*\(([^)]*)\)/g;

    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]);
        if (/\btantrum\s+/.test(stripped)) { continue; } // skip declarations
        if (/^\s*\/\//.test(lines[i])) { continue; }

        var cm;
        callRe.lastIndex = 0;
        while ((cm = callRe.exec(stripped)) !== null) {
            var fn = cm[1];
            if (skip[fn]) { continue; }
            var sig = funcSigs[fn];
            if (!sig) { continue; }

            var args = splitArgs(cm[2]);
            if (cm[2].trim() === '') { args = []; }

            // Arity check
            if (args.length !== sig.params.length) {
                var col = lines[i].indexOf(cm[0]);
                diags.push(makeDiag(i, col, col + cm[0].length,
                    "'" + fn + "' expects " + sig.params.length + " arg(s) but got " + args.length + ".",
                    vscode.DiagnosticSeverity.Error));
                continue;
            }

            // Type check each arg
            for (var j = 0; j < args.length && j < sig.params.length; j++) {
                var pt = sig.params[j].type;
                if (!pt) { continue; }
                var at = inferType(args[j].trim());
                if (!at) { continue; }
                if (!typeOk(pt, at)) {
                    var col2 = lines[i].indexOf(cm[0]);
                    diags.push(makeDiag(i, col2, col2 + cm[0].length,
                        "'" + fn + "' param " + (j + 1) + " ('" + sig.params[j].name + "') expects '" + pt + "' but got '" + at + "'.",
                        vscode.DiagnosticSeverity.Error));
                }
            }
        }
    }
}

// --- assignment type checking ---
function checkAssignmentTypes(lines, varDecls, diags) {
    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]).trim();
        // Typed declaration with wrong init: int x = "hello";
        var declMatch = stripped.match(/^(int|float|string|bool|list|map)\s+(\w+)\s*=\s*([^;]+)/);
        if (declMatch) {
            var declType = declMatch[1];
            var val = declMatch[3].trim();
            var valType = inferType(val);
            if (valType && !typeOk(declType, valType)) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "Cannot assign '" + valType + "' to '" + declType + "' variable '" + declMatch[2] + "'.",
                    vscode.DiagnosticSeverity.Error));
            }
            continue;
        }

        // Re-assignment to typed var: x = "hello"; where x was int
        var assignMatch = stripped.match(/^(\w+)\s*=\s*([^=;]+);?$/);
        if (!assignMatch) { continue; }
        var varName = assignMatch[1];
        var valExpr = assignMatch[2].trim();
        if (KEYWORDS.indexOf(varName) >= 0) { continue; }

        var info = varDecls[varName];
        if (!info || !info.typed) { continue; }
        var vt = inferType(valExpr);
        if (!vt) { continue; }
        if (!typeOk(info.type, vt)) {
            diags.push(makeDiag(i, 0, lines[i].length,
                "Cannot assign '" + vt + "' to '" + info.type + "' variable '" + varName + "'.",
                vscode.DiagnosticSeverity.Error));
        }
    }
}

// --- duplicate function definitions ---
function checkDuplicateFunctions(lines, diags) {
    var seen = {};
    var re = /\btantrum\s+(?:(?:int|float|string|bool|list|map)\s+)?(\w+)\s*\(/;
    for (var i = 0; i < lines.length; i++) {
        var m = lines[i].match(re);
        if (!m) { continue; }
        var name = m[1];
        if (seen[name] !== undefined) {
            var col = lines[i].indexOf(name);
            diags.push(makeDiag(i, col, col + name.length,
                "Duplicate function '" + name + "' — already defined on line " + (seen[name] + 1) + ".",
                vscode.DiagnosticSeverity.Error));
        } else {
            seen[name] = i;
        }
    }
}

// --- duplicate variable declarations (same function scope) ---
function checkDuplicateVariables(lines, diags) {
    var scopeStack = [{}]; // global scope
    var re = /\b(int|float|string|bool|list|map)\s+(\w+)\s*=/;

    for (var i = 0; i < lines.length; i++) {
        var trimmed = lines[i].trim();

        // Track scope with braces
        for (var c = 0; c < trimmed.length; c++) {
            if (trimmed[c] === '{') { scopeStack.push({}); }
            if (trimmed[c] === '}' && scopeStack.length > 1) { scopeStack.pop(); }
        }

        var m = trimmed.match(re);
        if (!m) { continue; }
        var name = m[2];
        var currentScope = scopeStack[scopeStack.length - 1];
        if (currentScope[name] !== undefined) {
            var col = lines[i].indexOf(name);
            diags.push(makeDiag(i, col, col + name.length,
                "Variable '" + name + "' already declared in this scope (line " + (currentScope[name] + 1) + ").",
                vscode.DiagnosticSeverity.Warning));
        } else {
            currentScope[name] = i;
        }
    }
}

// --- undefined function calls ---
function checkUndefinedFunctions(lines, funcSigs, diags) {
    var skip = {
        'print': 1, 'input': 1, 'len': 1, 'range': 1, 'type': 1, 'append': 1,
        'tantrum': 1, 'if': 1, 'while': 1, 'for': 1, 'catch': 1, 'alloc': 1, 'free': 1,
        'int': 1, 'float': 1, 'string': 1, 'bool': 1, 'list': 1, 'map': 1
    };
    var callRe = /\b(\w+)\s*\(/g;

    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]);
        if (/\btantrum\s+/.test(stripped)) { continue; }
        if (/^\s*\/\//.test(lines[i])) { continue; }
        if (/^\s*\*/.test(lines[i])) { continue; }

        var m;
        callRe.lastIndex = 0;
        while ((m = callRe.exec(stripped)) !== null) {
            var fn = m[1];
            if (skip[fn]) { continue; }
            if (funcSigs[fn]) { continue; }
            // Might be a variable (ignore single-char or common patterns)
            var col = lines[i].indexOf(m[0]);
            diags.push(makeDiag(i, col, col + fn.length,
                "'" + fn + "' is not defined. Did you forget to declare it with 'tantrum'?",
                vscode.DiagnosticSeverity.Error));
        }
    }
}

// --- undefined variables (basic) ---
function checkUndefinedVariables(lines, funcSigs, varDecls, diags) {
    // Collect all known names: functions, params, for-loop vars, globals
    var known = {};
    var builtinNames = ['print', 'input', 'len', 'range', 'type', 'append', 'true', 'false', 'null',
        'tantrum', 'if', 'else', 'while', 'for', 'in', 'return', 'throw',
        'alloc', 'free', 'use', 'and', 'or', 'try', 'catch',
        'int', 'float', 'string', 'bool', 'list', 'map', 'main', 'i'];

    for (var b = 0; b < builtinNames.length; b++) { known[builtinNames[b]] = true; }
    for (var fn in funcSigs) { known[fn] = true; }
    for (var v in varDecls) { known[v] = true; }

    // Collect param names and for-loop vars
    var paramRe = /\btantrum\s+(?:(?:int|float|string|bool|list|map)\s+)?(\w+)\s*\(([^)]*)\)/;
    var forRe = /\bfor\s+(\w+)\s+in\b/;
    var catchRe = /\bcatch\s*\(\s*(\w+)\s*\)/;
    for (var i = 0; i < lines.length; i++) {
        var pm = lines[i].match(paramRe);
        if (pm && pm[2]) {
            var ps = pm[2].split(',');
            for (var p = 0; p < ps.length; p++) {
                var pp = ps[p].trim().split(/\s+/);
                known[pp[pp.length - 1]] = true;
            }
        }
        var fm = lines[i].match(forRe);
        if (fm) { known[fm[1]] = true; }
        var cm = lines[i].match(catchRe);
        if (cm) { known[cm[1]] = true; }
        // Dynamic assignments create variables
        var dynMatch = stripComments(lines[i]).trim().match(/^(\w+)\s*=\s*[^=]/);
        if (dynMatch && KEYWORDS.indexOf(dynMatch[1]) < 0) { known[dynMatch[1]] = true; }
    }

    // Now check for uses of identifiers that aren't known
    // Only check inside the RHS of assignments and function arguments
    // This is deliberately conservative to avoid false positives
    var identRe = /\b([a-zA-Z_]\w*)\b/g;
    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]).trim();
        if (stripped === '' || stripped === '{' || stripped === '}') { continue; }
        if (/^\s*\/\//.test(lines[i]) || /^#/.test(stripped)) { continue; }
        if (/^tantrum\s+/.test(stripped)) { continue; }
        if (/^use\s+/.test(stripped)) { continue; }

        // Check RHS of assignments and function args for unknown identifiers
        var rhsMatch = stripped.match(/=\s*(.+?);\s*$/);
        var toCheck = rhsMatch ? rhsMatch[1] : null;
        // Also check standalone expression lines like print(unknownVar);
        if (!toCheck && /^\w+\s*\(/.test(stripped)) {
            var parenContent = stripped.match(/\(([^)]+)\)/);
            if (parenContent) { toCheck = parenContent[1]; }
        }
        if (!toCheck) { continue; }

        var im;
        identRe.lastIndex = 0;
        while ((im = identRe.exec(toCheck)) !== null) {
            var id = im[1];
            if (known[id]) { continue; }
            if (/^\d/.test(id)) { continue; } // skip numbers
            // Check if it's just a string literal context
            if (toCheck.indexOf('"') >= 0) { continue; } // skip lines with strings (too many false positives)
            var col = lines[i].indexOf(id);
            if (col < 0) { continue; }
            diags.push(makeDiag(i, col, col + id.length,
                "'" + id + "' may be undefined.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

// --- return/throw outside function ---
function checkReturnOutsideFunction(lines, diags) {
    var inFunc = 0;
    for (var i = 0; i < lines.length; i++) {
        var trimmed = lines[i].trim();
        if (/^tantrum\s+/.test(trimmed)) { inFunc = 0; }
        for (var c = 0; c < trimmed.length; c++) {
            if (trimmed[c] === '{') { inFunc++; }
            if (trimmed[c] === '}') { inFunc--; }
        }
        if (inFunc <= 0) {
            var retMatch = trimmed.match(/^\s*return\b/);
            if (retMatch) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "'return' used outside of a function.",
                    vscode.DiagnosticSeverity.Error));
            }
        }
    }
}

// --- dead code after return ---
function checkDeadCode(lines, diags) {
    var inFunc = false;
    var afterReturn = false;
    var braceDepth = 0;
    var returnDepth = -1;

    for (var i = 0; i < lines.length; i++) {
        var trimmed = lines[i].trim();

        if (/^tantrum\s+/.test(trimmed)) {
            inFunc = true;
            afterReturn = false;
            braceDepth = 0;
            returnDepth = -1;
        }

        for (var c = 0; c < trimmed.length; c++) {
            if (trimmed[c] === '{') { braceDepth++; }
            if (trimmed[c] === '}') {
                if (afterReturn && braceDepth <= returnDepth) {
                    afterReturn = false;
                }
                braceDepth--;
            }
        }

        if (afterReturn && trimmed !== '' && trimmed !== '}' && trimmed !== '{') {
            diags.push(makeDiag(i, 0, lines[i].length,
                "Unreachable code after 'return'.",
                vscode.DiagnosticSeverity.Warning));
        }

        if (inFunc && /^\s*return\b/.test(trimmed)) {
            afterReturn = true;
            returnDepth = braceDepth;
        }
    }
}

// --- division by zero ---
function checkDivisionByZero(lines, diags) {
    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]);
        // Match: / 0 or /0 (not inside strings)
        var m = stripped.match(/\/\s*0(?:\.0+)?\s*[;)\s,]/);
        if (m) {
            // Make sure it's not inside a string
            var idx = stripped.indexOf(m[0]);
            if (!isInsideString(stripped, idx)) {
                diags.push(makeDiag(i, idx, idx + m[0].length,
                    "Division by zero.",
                    vscode.DiagnosticSeverity.Error));
            }
        }
    }
}

// --- missing return in non-void function ---
function checkMissingReturn(lines, funcSigs, diags) {
    var re = /\btantrum\s+(int|float|string|bool|list|map)\s+(\w+)\s*\(/;
    var funcStart = -1;
    var funcName = '';
    var funcLine = -1;
    var braceDepth = 0;
    var hasReturn = false;

    for (var i = 0; i < lines.length; i++) {
        var trimmed = lines[i].trim();
        var m = trimmed.match(re);

        if (m) {
            // Check previous function
            if (funcName && !hasReturn && funcStart >= 0) {
                diags.push(makeDiag(funcLine, 0, lines[funcLine].length,
                    "Function '" + funcName + "' has return type but may not return a value.",
                    vscode.DiagnosticSeverity.Warning));
            }
            funcName = m[2];
            funcLine = i;
            funcStart = -1;
            braceDepth = 0;
            hasReturn = false;
        }

        for (var c = 0; c < trimmed.length; c++) {
            if (trimmed[c] === '{') {
                if (funcStart === -1) { funcStart = i; }
                braceDepth++;
            }
            if (trimmed[c] === '}') {
                braceDepth--;
                if (braceDepth === 0 && funcStart >= 0) {
                    // End of function
                    if (funcName && !hasReturn) {
                        diags.push(makeDiag(funcLine, 0, lines[funcLine].length,
                            "Function '" + funcName + "' has return type but may not return a value.",
                            vscode.DiagnosticSeverity.Warning));
                    }
                    funcName = '';
                    funcStart = -1;
                }
            }
        }

        if (/^\s*return\b/.test(trimmed) && funcName) {
            hasReturn = true;
        }
    }
}

// --- unused variables (warning) ---
function checkUnusedVariables(lines, varDecls, diags) {
    var text = lines.join('\n');
    for (var name in varDecls) {
        var info = varDecls[name];
        if (!info.typed) { continue; } // only check typed declarations

        // Count occurrences (excluding the declaration line itself)
        var re = new RegExp('\\b' + name + '\\b', 'g');
        var matches = text.match(re);
        var count = matches ? matches.length : 0;

        // If only appears once (the declaration), it's unused
        if (count <= 1) {
            var col = lines[info.line].indexOf(name);
            diags.push(makeDiag(info.line, col, col + name.length,
                "Variable '" + name + "' is declared but never used.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

// --- shadowing built-in keywords ---
function checkShadowBuiltins(lines, diags) {
    var builtins = ['print', 'input', 'len', 'range', 'type', 'append'];
    var declRe = /\b(int|float|string|bool|list|map)\s+(\w+)\s*=/;

    for (var i = 0; i < lines.length; i++) {
        var m = lines[i].match(declRe);
        if (!m) { continue; }
        var name = m[2];
        if (builtins.indexOf(name) >= 0) {
            var col = lines[i].indexOf(name, lines[i].indexOf(m[1]) + m[1].length);
            diags.push(makeDiag(i, col, col + name.length,
                "'" + name + "' shadows a built-in function.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

// --- empty blocks ---
function checkEmptyBlocks(lines, diags) {
    for (var i = 0; i < lines.length - 1; i++) {
        var trimmed = lines[i].trim();
        var nextTrimmed = (i + 1 < lines.length) ? lines[i + 1].trim() : '';
        // Check {  immediately followed by }
        if (trimmed.endsWith('{') && nextTrimmed === '}') {
            // Only warn if it's a control structure, not function decl
            if (/\b(if|else|while|for|try|catch)\b/.test(trimmed)) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "Empty block body.",
                    vscode.DiagnosticSeverity.Warning));
            }
        }
    }
}


// ============================================================
//  HOVER
// ============================================================

var HOVER_DOCS = {
    'print': { sig: 'print(value)', desc: 'Prints any value to the console.' },
    'input': { sig: 'input(prompt?)', desc: 'Reads a line from stdin. Auto-casts to typed variable.' },
    'len': { sig: 'len(value) -> int', desc: 'Returns length of a string, list, or map.' },
    'range': { sig: 'range(n) -> list', desc: 'Generates list [0, 1, ..., n-1].' },
    'type': { sig: 'type(value) -> string', desc: 'Returns the type name as a string.' },
    'append': { sig: 'append(list, value)', desc: 'Appends a value to a list in-place.' },
    'tantrum': { sig: 'tantrum [type] name(params) { ... }', desc: '**Function declaration.** Optional return type.' },
    'if': { sig: 'if (condition) { ... }', desc: '**Conditional.** Executes block if truthy.' },
    'else': { sig: 'else { ... }', desc: '**Else clause.**' },
    'while': { sig: 'while (condition) { ... }', desc: '**While loop.**' },
    'for': { sig: 'for var in iterable { ... }', desc: '**For-in loop.** Iterates list, string, range.' },
    'in': { sig: 'for var in iterable', desc: '**In keyword.** Used in for-in loops.' },
    'return': { sig: 'return value;', desc: '**Return.** Returns from function.' },
    'throw': { sig: 'throw "message";', desc: '**Throw.** If inside try, jumps to catch. Otherwise halts.' },
    'try': { sig: 'try { ... } catch (e) { ... }', desc: '**Try.** Catches errors thrown inside.' },
    'catch': { sig: 'catch (e) { ... }', desc: '**Catch.** Handles errors. Error var optional.' },
    'alloc': { sig: 'alloc type(value)', desc: '**Heap allocate.** Must free with free.' },
    'free': { sig: 'free(pointer);', desc: '**Free.** Deallocates heap pointer.' },
    'use': { sig: 'use filename.42AHH;', desc: '**Import.** Imports from another file (same dir).' },
    'true': { sig: 'true', desc: '**Boolean literal.**' },
    'false': { sig: 'false', desc: '**Boolean literal.**' },
    'null': { sig: 'null', desc: '**Null literal.**' },
    'and': { sig: 'expr and expr', desc: '**Logical AND.** Short-circuit.' },
    'or': { sig: 'expr or expr', desc: '**Logical OR.** Short-circuit.' },
    'int': { sig: 'int', desc: '**Integer type.** 64-bit signed. `int x = 42;`' },
    'float': { sig: 'float', desc: '**Float type.** 64-bit double. `float pi = 3.14;`' },
    'string': { sig: 'string', desc: '**String type.** UTF-8, escape sequences supported.' },
    'bool': { sig: 'bool', desc: '**Boolean type.** true or false.' },
    'list': { sig: 'list', desc: '**List type.** `list x = [1, 2, 3];`' },
    'map': { sig: 'map', desc: '**Map type.** `map x = {"a": 1};`' }
};

var MODE_INFO = {
    'static': '`#mode static;` — All variables **must** have type annotations.',
    'dynamic': '`#mode dynamic;` — No type checking at all.',
    'both': '`#mode both;` — Default. Typed vars checked, untyped are dynamic.'
};

function doHover(doc, pos) {
    var lineText = doc.lineAt(pos).text;

    var modeMatch = lineText.match(/#mode\s+(static|dynamic|both)/);
    if (modeMatch) {
        var md = new vscode.MarkdownString();
        md.appendCodeblock('#mode ' + modeMatch[1] + ';', 'tantrums');
        md.appendMarkdown('\n\n' + MODE_INFO[modeMatch[1]]);
        return new vscode.Hover(md);
    }

    var range = doc.getWordRangeAtPosition(pos);
    if (!range) { return null; }
    var word = doc.getText(range);
    var info = HOVER_DOCS[word];
    if (!info) { return null; }

    var md2 = new vscode.MarkdownString();
    md2.appendCodeblock(info.sig, 'tantrums');
    md2.appendMarkdown('\n\n' + info.desc);
    return new vscode.Hover(md2, range);
}


// ============================================================
//  COMPLETION
// ============================================================

function doComplete(doc, pos) {
    var items = [];

    var kws = ['tantrum', 'if', 'else', 'while', 'for', 'in', 'return', 'throw',
        'alloc', 'free', 'use', 'and', 'or', 'true', 'false', 'null', 'try', 'catch'];
    for (var k = 0; k < kws.length; k++) {
        var ki = new vscode.CompletionItem(kws[k], vscode.CompletionItemKind.Keyword);
        ki.detail = 'Keyword';
        items.push(ki);
    }

    var types = ['int', 'float', 'string', 'bool', 'list', 'map'];
    for (var t = 0; t < types.length; t++) {
        var ti = new vscode.CompletionItem(types[t], vscode.CompletionItemKind.TypeParameter);
        ti.detail = 'Type';
        items.push(ti);
    }

    var builtins = [
        { name: 'print', insert: 'print(${1:value})', sig: 'print(value)' },
        { name: 'input', insert: 'input(${1:"prompt"})', sig: 'input(prompt?)' },
        { name: 'len', insert: 'len(${1:value})', sig: 'len(value) -> int' },
        { name: 'range', insert: 'range(${1:n})', sig: 'range(n) -> list' },
        { name: 'type', insert: 'type(${1:value})', sig: 'type(value) -> string' },
        { name: 'append', insert: 'append(${1:list}, ${2:value})', sig: 'append(list, value)' }
    ];
    for (var b = 0; b < builtins.length; b++) {
        var bi = new vscode.CompletionItem(builtins[b].name, vscode.CompletionItemKind.Function);
        bi.detail = builtins[b].sig;
        bi.insertText = new vscode.SnippetString(builtins[b].insert);
        items.push(bi);
    }

    // User functions
    var text = doc.getText();
    var funcRe = /\btantrum\s+(?:(?:int|float|string|bool|list|map)\s+)?(\w+)\s*\(([^)]*)\)/g;
    var fm;
    while ((fm = funcRe.exec(text)) !== null) {
        if (fm[1] === 'main') { continue; }
        var fi = new vscode.CompletionItem(fm[1], vscode.CompletionItemKind.Function);
        fi.detail = 'tantrum ' + fm[1] + '(' + fm[2] + ')';
        var pp = fm[2].split(',');
        var sp = [];
        for (var p = 0; p < pp.length; p++) {
            var pn = pp[p].trim().split(/\s+/);
            if (pn[0] !== '') { sp.push('${' + (p + 1) + ':' + pn[pn.length - 1] + '}'); }
        }
        fi.insertText = new vscode.SnippetString(fm[1] + '(' + sp.join(', ') + ')');
        items.push(fi);
    }

    return items;
}


// ============================================================
//  COMMANDS
// ============================================================

function runCmd(mode) {
    var editor = vscode.window.activeTextEditor;
    if (!editor) { vscode.window.showErrorMessage('No file open.'); return; }
    var fp = editor.document.fileName;
    if (mode === 'exec' && !fp.endsWith('.42ass')) {
        vscode.window.showErrorMessage('Not a .42ass file.'); return;
    }
    if (mode !== 'exec' && !fp.endsWith('.42AHH') && !fp.endsWith('.42ahh')) {
        vscode.window.showErrorMessage('Not a .42AHH file.'); return;
    }

    editor.document.save().then(function () {
        var term = vscode.window.terminals.find(function (t) { return t.name === 'Tantrums'; });
        if (!term) { term = vscode.window.createTerminal('Tantrums'); }
        term.show();
        term.sendText('tantrums ' + mode + ' "' + fp + '"');
    });
}


// ============================================================
//  HELPERS
// ============================================================

function stripComments(line) {
    var result = '';
    var inStr = false;
    for (var i = 0; i < line.length; i++) {
        if (inStr) {
            if (line[i] === '\\') { result += line[i] + (line[i + 1] || ''); i++; continue; }
            if (line[i] === '"') { inStr = false; result += '"'; continue; }
            result += line[i]; continue;
        }
        if (line[i] === '"') { inStr = true; result += '"'; continue; }
        if (line[i] === '/' && line[i + 1] === '/') { break; }
        if (line[i] === '/' && line[i + 1] === '*') {
            var end = line.indexOf('*/', i + 2);
            if (end !== -1) { i = end + 1; continue; }
            break;
        }
        result += line[i];
    }
    return result;
}

function splitArgs(str) {
    var args = [];
    var depth = 0;
    var current = '';
    for (var i = 0; i < str.length; i++) {
        if (str[i] === '(' || str[i] === '[') { depth++; }
        if (str[i] === ')' || str[i] === ']') { depth--; }
        if (str[i] === ',' && depth === 0) { args.push(current); current = ''; continue; }
        current += str[i];
    }
    if (current.trim()) { args.push(current); }
    return args;
}

function inferType(expr) {
    var e = expr.trim();
    if (e.charAt(0) === '"') { return 'string'; }
    if (e === 'true' || e === 'false') { return 'bool'; }
    if (e === 'null') { return 'null'; }
    if (/^\d+\.\d+$/.test(e)) { return 'float'; }
    if (/^-?\d+$/.test(e)) { return 'int'; }
    if (e.charAt(0) === '[') { return 'list'; }
    if (e.charAt(0) === '{') { return 'map'; }
    return null;
}

function typeOk(expected, actual) {
    if (expected === actual) { return true; }
    if (expected === 'float' && actual === 'int') { return true; }
    return false;
}

function isInsideString(line, index) {
    var inStr = false;
    for (var i = 0; i < index; i++) {
        if (line[i] === '"' && (i === 0 || line[i - 1] !== '\\')) { inStr = !inStr; }
    }
    return inStr;
}

function makeDiag(line, startCol, endCol, message, severity) {
    return new vscode.Diagnostic(
        new vscode.Range(line, startCol, line, endCol),
        message, severity
    );
}

// --- unused variables (warning) --- checks both typed AND dynamic
function checkUnusedVariables(lines, varDecls, diags) {
    var text = lines.join('\n');
    for (var name in varDecls) {
        var info = varDecls[name];

        // Count occurrences (excluding the declaration line itself)
        var re = new RegExp('\\b' + name + '\\b', 'g');
        var matches = text.match(re);
        var count = matches ? matches.length : 0;

        // If only appears once (the declaration), it's unused
        if (count <= 1) {
            var col = lines[info.line].indexOf(name);
            var label = info.typed ? (info.type + ' ') : '';
            diags.push(makeDiag(info.line, col, col + name.length,
                "Variable '" + label + name + "' is declared but never used.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

// --- shadowing built-in keywords ---
function checkShadowBuiltins(lines, diags) {
    var builtins = ['print', 'input', 'len', 'range', 'type', 'append'];
    var declRe = /\b(?:int|float|string|bool|list|map)\s+(\w+)\s*=/;
    var dynRe = /^(\w+)\s*=\s*[^=]/;

    for (var i = 0; i < lines.length; i++) {
        var stripped = stripComments(lines[i]).trim();
        var m = stripped.match(declRe);
        var name = m ? m[1] : null;
        if (!name) {
            var dm = stripped.match(dynRe);
            name = dm ? dm[1] : null;
        }
        if (name && builtins.indexOf(name) >= 0) {
            var col = lines[i].indexOf(name);
            diags.push(makeDiag(i, col, col + name.length,
                "'" + name + "' shadows a built-in function.",
                vscode.DiagnosticSeverity.Warning));
        }
    }
}

// --- empty blocks ---
function checkEmptyBlocks(lines, diags) {
    for (var i = 0; i < lines.length - 1; i++) {
        var trimmed = lines[i].trim();
        var nextTrimmed = (i + 1 < lines.length) ? lines[i + 1].trim() : '';
        if (trimmed.endsWith('{') && nextTrimmed === '}') {
            if (/\b(if|else|while|for|try|catch)\b/.test(trimmed)) {
                diags.push(makeDiag(i, 0, lines[i].length,
                    "Empty block body.",
                    vscode.DiagnosticSeverity.Warning));
            }
        }
    }
}

module.exports = { activate: activate, deactivate: deactivate };
