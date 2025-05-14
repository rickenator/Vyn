#ifndef VYN_HPP
#define VYN_HPP

#include "lexer.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "token.hpp"

// EBNF Grammar of the Vyn Language
//
// Conventions:
//   IDENTIFIER:        Represents a valid identifier token.
//   INTEGER_LITERAL:   Represents an integer literal token.
//   FLOAT_LITERAL:     Represents a float literal token.
//   STRING_LITERAL:    Represents a string literal token.
//   BOOLEAN_LITERAL:   Represents 'true' or 'false'.
//   'keyword':         Denotes a literal keyword.
//   { ... }:           Represents zero or more occurrences (Kleene star).
//   [ ... ]:           Represents zero or one occurrence (optional).
//   ( ... | ... ):     Represents a choice (alternation).
//   ... ::= ... :      Defines a production rule.
//
// Grammar:
module                 ::= { module_item } EOF

module_item            ::= import_statement
                         | smuggle_statement
                         | class_declaration
                         | struct_declaration
                         | enum_declaration
                         | impl_declaration
                         | function_declaration
                         | variable_declaration
                         | constant_declaration
                         | type_alias_declaration

// Top-level declarations
import_statement       ::= 'import' path [ 'as' IDENTIFIER ] ';'
smuggle_statement      ::= 'smuggle' path [ 'as' IDENTIFIER ] ';' // Assuming similar to import
path                   ::= IDENTIFIER { '::' IDENTIFIER }

class_declaration      ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'class' IDENTIFIER [ 'extends' type ] [ 'implements' type_list ] '{' { class_member } '}'
class_member           ::= field_declaration | method_declaration | constructor_declaration
field_declaration      ::= [ 'pub' ] ( 'var' | 'const' ) IDENTIFIER ':' type [ '=' expression ] ';'
method_declaration     ::= [ 'pub' ] [ 'static' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ] 'fn' IDENTIFIER '(' [ parameter_list ] ')' [ '->' type ] block_statement
constructor_declaration::= [ 'pub' ] 'new' [ template_parameters ] '(' [ parameter_list ] ')' block_statement


struct_declaration     ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'struct' IDENTIFIER '{' { struct_field_declaration } '}'
struct_field_declaration ::= [ 'pub' ] IDENTIFIER ':' type [ '=' expression ] ';'

enum_declaration       ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'enum' IDENTIFIER '{' { enum_variant } '}'
enum_variant           ::= IDENTIFIER [ '(' type_list ')' ] [ '=' expression ] ','?

impl_declaration       ::= [ 'template' '<' type_parameter_list '>' ] 'impl' type [ 'for' type ] '{' { method_declaration } '}'

function_declaration   ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ] 'fn' IDENTIFIER '(' [ parameter_list ] ')' [ '->' type ] block_statement

variable_declaration   ::= [ 'pub' ] 'var' IDENTIFIER [ ':' type ] [ '=' expression ] ';'
constant_declaration   ::= [ 'pub' ] 'const' IDENTIFIER ':' type '=' expression ';'

type_alias_declaration ::= [ 'pub' ] 'type' IDENTIFIER [ template_parameters ] '=' type '; // Changed from [ type_parameters ] for consistency


// Parameters and Type Lists
type_parameter_list    ::= type_parameter { ',' type_parameter }
type_parameter         ::= IDENTIFIER [ ':' type_bounds ]
type_bounds            ::= type { '+' type }
template_parameters    ::= '<' type_parameter_list '>' // Alias for consistency if used in constructor

parameter_list         ::= parameter { ',' parameter }
parameter              ::= [ 'ref' [ 'mut' ] ] IDENTIFIER ':' type [ '=' expression ]

type_list              ::= type { ',' type }

// Statements
statement              ::= expression_statement
                         | block_statement
                         | if_statement
                         | for_statement
                         | while_statement
                         | loop_statement
                         | match_statement
                         | return_statement
                         | break_statement
                         | continue_statement
                         | defer_statement
                         | try_statement
                         | variable_declaration
                         | constant_declaration

expression_statement   ::= expression ';'
block_statement        ::= '{' { statement } '}'

if_statement           ::= 'if' expression block_statement { 'else' 'if' expression block_statement } [ 'else' block_statement ]
for_statement          ::= 'for' pattern 'in' expression block_statement
while_statement        ::= 'while' expression block_statement
loop_statement         ::= 'loop' block_statement
match_statement        ::= 'match' expression '{' { match_arm } '}'
match_arm              ::= pattern [ 'if' expression ] '=>' ( expression | block_statement ) ','?
pattern                ::= IDENTIFIER [ '@' pattern ]
                         | literal
                         | '_'
                         | path '{' [ field_pattern { ',' field_pattern } [','] ] '}' // Struct pattern
                         | path '(' [ pattern_list ] ')' // Enum variant pattern
                         | '[' [ pattern_list ] ']' // Array/slice pattern
                         | '(' pattern_list ')' // Tuple pattern
                         | 'ref' [ 'mut' ] pattern
field_pattern          ::= IDENTIFIER ':' pattern | IDENTIFIER
pattern_list           ::= pattern { ',' pattern }

return_statement       ::= 'return' [ expression ] ';'
break_statement        ::= 'break' [ IDENTIFIER ] [ expression ] ';'
continue_statement     ::= 'continue' [ IDENTIFIER ] ';'
defer_statement        ::= 'defer' ( expression_statement | block_statement )
try_statement          ::= 'try' block_statement { catch_clause } [ 'finally' block_statement ]
catch_clause           ::= 'catch' [ IDENTIFIER ] [ ':' type ] block_statement

// Expressions
expression             ::= assignment_expression
assignment_expression  ::= conditional_expression [ assignment_operator assignment_expression ] // Updated to use conditional_expression
assignment_operator    ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>='

conditional_expression ::= logical_or_expression [ '?' expression ':' conditional_expression ] // TBD: Ternary operator

logical_or_expression  ::= logical_and_expression { '||' logical_and_expression }
logical_and_expression ::= bitwise_or_expression { '&&' bitwise_or_expression }
bitwise_or_expression  ::= bitwise_xor_expression { '|' bitwise_xor_expression }
bitwise_xor_expression ::= bitwise_and_expression { '^' bitwise_and_expression }
bitwise_and_expression ::= equality_expression { '&' equality_expression }
equality_expression    ::= relational_expression { ( '==' | '!=' ) relational_expression }
relational_expression  ::= shift_expression { ( '<' | '<=' | '>' | '>=' | 'is' | 'as' ) shift_expression }
shift_expression       ::= additive_expression { ( '<<' | '>>' ) additive_expression }
additive_expression    ::= multiplicative_expression { ( '+' | '-' ) multiplicative_expression }
multiplicative_expression ::= unary_expression { ( '*' | '/' | '%' ) unary_expression }

unary_expression       ::= ( '!' | '-' | '+' | '*' | '&' [ 'mut' ] | 'await' ) unary_expression
                         | primary_expression

primary_expression     ::= literal
                         | path_expression // IDENTIFIER alternative removed as it's covered by path_expression
                         | '(' expression ')'
                         | call_expression
                         | member_access_expression
                         | index_access_expression
                         | list_comprehension
                         | array_literal
                         | tuple_literal
                         | struct_literal
                         | lambda_expression
                         | 'self' | 'super'

literal                ::= INTEGER_LITERAL | FLOAT_LITERAL | STRING_LITERAL | BOOLEAN_LITERAL | 'null'

path_expression        ::= path [ type_arguments ]

call_expression        ::= primary_expression '(' [ argument_list ] ')' [ '?' ]
argument_list          ::= expression { ',' expression }

member_access_expression ::= primary_expression ( '.' | '?.' ) IDENTIFIER
index_access_expression  ::= primary_expression '[' expression ']' [ '?' ]

list_comprehension     ::= '[' expression 'for' pattern 'in' expression [ 'if' expression ] ']'
array_literal          ::= '[' [ expression { ',' expression } [','] ] ']'
                         | '[' expression ';' expression ']'

tuple_literal          ::= '(' [ expression { ',' expression } [ ',' ] ] ')'

struct_literal         ::= path_expression '{' [ struct_literal_field { ',' struct_literal_field } [ ',' ] ] '}'
struct_literal_field   ::= IDENTIFIER ':' expression | IDENTIFIER

lambda_expression      ::= [ 'async' ] ( '|' [ parameter_list ] '|' | IDENTIFIER ) [ '->' type ] ( '=>' expression | block_statement )

// Type System:
type                   ::= core_type [ '?' ]
core_type              ::= named_type
                         | array_type
                         | slice_type
                         | tuple_type
                         | function_type
                         | reference_type
                         | pointer_type
                         | 'self' | 'Self'
                         | '_' | '!'
                         | '(' type ')'

named_type             ::= path [ type_arguments ]
type_arguments         ::= '<' type_list '>'

array_type             ::= '[' type ';' expression ']'
slice_type             ::= '[' type ']'

tuple_type             ::= '(' [ type_list [ ',' ] ] ')'

function_type          ::= [ 'async' ] 'fn' '(' [ type_list ] ')' [ '->' type ]

reference_type         ::= '&' [ lifetime ] [ 'mut' ] type
                         | 'ref' '<' type '>'

pointer_type           ::= '*' ( 'const' | 'mut' ) type

lifetime               ::= APOSTROPHE IDENTIFIER

// End of EBNF Grammar
//
// Note on 'box' keyword and 'while let':
// The 'box' keyword for heap allocation (e.g., 'box Type { ... }') and the
// 'while let' construct (e.g., 'while let Some(x) = option_val { ... }')
// are present in some example files but are NOT currently supported by the parser
// and are therefore not included in this EBNF. They represent planned or
// experimental features.
//
// Note on struct literal with type arguments:
// The parser expects struct literals like 'StructName { field: value }'.
// Explicit type arguments on the literal itself (e.g. 'StructName<T> { ... }')
// are not supported by the parser for struct literals; type arguments are resolved
// from context or the variable's type annotation.

#endif // VYN_HPP