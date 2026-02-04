module.exports = grammar({
  name: 'alkyl',

  extras: $ => [
    /\s/,
    $.line_comment,
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._top_level_item),

    _top_level_item: $ => choice(
      $.link_directive,
      $.import_directive,
      $.extern_declaration,
      $.function_definition,
      $.statement,
    ),

    // --- Directives ---

    link_directive: $ => seq(
      'link',
      field('library', choice($.identifier, $.string_literal)),
      ';'
    ),

    import_directive: $ => seq(
      'import',
      field('path', $.string_literal),
      ';'
    ),

    extern_declaration: $ => seq(
      'extern',
      field('type', $.type),
      field('name', $.identifier),
      '(',
      optional($.parameter_list),
      ')',
      ';'
    ),

    // --- Definitions ---

    function_definition: $ => seq(
      field('return_type', $.type),
      field('name', $.identifier),
      '(',
      optional($.parameter_list),
      ')',
      field('body', $.block)
    ),

    parameter_list: $ => seq(
      $.parameter,
      repeat(seq(',', $.parameter)),
      optional(seq(',', '...'))
    ),

    parameter: $ => seq(
      field('type', $.type),
      field('name', $.identifier)
    ),

    variable_declaration: $ => seq(
      optional(choice('mut', 'mutable', 'imut', 'immutable')),
      field('type', $.type),
      field('name', $.identifier),
      optional(seq(
        '[', 
        optional(field('size', $.expression)),
        ']'
      )),
      optional(seq(
        '=',
        field('value', $.expression)
      )),
      ';'
    ),

    // --- Statements ---

    block: $ => seq(
      '{',
      repeat($.statement),
      '}'
    ),

    statement: $ => choice(
      $.variable_declaration,
      $.return_statement,
      $.break_statement,
      $.continue_statement,
      $.if_statement,
      $.loop_statement,
      $.while_statement,
      $.assignment_statement,
      $.expression_statement,
      $.block
    ),

    return_statement: $ => seq(
      'return',
      optional($.expression),
      ';'
    ),

    break_statement: $ => seq('break', ';'),
    continue_statement: $ => seq('continue', ';'),

    // Added prec.right to resolve the "Dangling Else" conflict
    if_statement: $ => prec.right(seq(
      'if',
      field('condition', $.expression),
      field('consequence', $.statement),
      optional(seq(
        choice('else', 'elif'),
        field('alternative', $.statement)
      ))
    )),

    loop_statement: $ => seq(
      'loop',
      field('count', $.expression),
      field('body', $.statement)
    ),

    while_statement: $ => seq(
      'while',
      optional('once'), // do-while style
      field('condition', $.expression),
      field('body', $.statement)
    ),

    assignment_statement: $ => seq(
      field('left', $.expression),
      choice('=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='),
      field('right', $.expression),
      ';'
    ),

    expression_statement: $ => seq(
      $.expression,
      ';'
    ),

    // --- Expressions ---

    expression: $ => choice(
      $.binary_expression,
      $.unary_expression,
      $.update_expression,
      $.call_expression,
      $.array_access,
      $.identifier,
      $.literal,
      $.array_literal,
      $.parenthesized_expression
    ),

    // Operator Precedence Levels
    // 1: Binary (weakest)
    // 2: Unary
    // 3: Call
    // 4: Array Access
    // 5: Update (strongest)

    binary_expression: $ => prec.left(1, seq(
      field('left', $.expression),
      field('operator', choice('+', '-', '*', '/', '%', '==', '!=', '<', '>', '<=', '>=', '&&', '||', '&', '|', '^', '<<', '>>')),
      field('right', $.expression)
    )),

    unary_expression: $ => prec.right(2, choice(
      seq(choice('-', '!', '~'), $.expression),
      seq(choice('++', '--'), $.expression) // Prefix
    )),

    update_expression: $ => prec.left(5, seq(
      $.expression,
      choice('++', '--') // Postfix
    )),

    call_expression: $ => prec(3, seq(
      field('function', $.identifier),
      '(',
      optional($.argument_list),
      ')'
    )),

    argument_list: $ => seq(
      $.expression,
      repeat(seq(',', $.expression))
    ),

    array_access: $ => prec(4, seq(
      field('array', $.expression),
      '[',
      field('index', $.expression),
      ']'
    )),

    parenthesized_expression: $ => seq(
      '(',
      $.expression,
      ')'
    ),

    array_literal: $ => seq(
      '[',
      optional(seq(
        $.expression,
        repeat(seq(',', $.expression))
      )),
      ']'
    ),

    // --- Atoms ---

    type: $ => choice(
      'void', 'int', 'char', 'bool', 'single', 'double', 'let'
    ),

    literal: $ => choice(
      $.number_literal,
      $.string_literal,
      $.char_literal,
      $.boolean_literal
    ),

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
    number_literal: $ => /\d+(\.\d+)?/,
    string_literal: $ => /"[^"]*"/,
    char_literal: $ => /'[^']'/,
    boolean_literal: $ => choice('true', 'false'),
    line_comment: $ => /\/\/.*/,
  }
});
