#ifndef VYB_HPP
#define VYB_HPP

/*
 * Vyb Programming Language v0.4.1
 *
 * CURRENT IMPLEMENTATION STATUS:
 * ✅ Complete LLVM backend with JIT execution
 * ✅ Auto-serialization for complex return types
 * ✅ Functions, variables, structs, control flow (if/else, while, for)
 * ✅ Fixed-size arrays [T; N] with indexing and beautiful println() serialization
 * ✅ Dynamic vectors Vec<T> with full method support (new, push, pop, len, get)
 * ✅ Vec iteration with for loops and break/continue
 * ✅ Range-based for loops (0..10 inclusive ranges)
 * ✅ Pattern matching with match statements (-> syntax, ? wildcard, comparison patterns)
 * ✅ Select expressions with pass keyword (expression-based pattern matching)
 * ✅ Comparison patterns (>=, <=, <, >, ==, !=) with unreachable pattern detection
 * ✅ Type system with ownership (my<T>, our<T>, their<T>)
 * ✅ Memory safety with borrowing (view, borrow) and freedom blocks
 * ✅ Comprehensive parser supporting templates, async, classes
 * 📋 Standard library modules (planned)
 *
 * This header provides the main interface for Vyb compilation and execution.
 */

#include "vyb/parser/lexer.hpp"
#include "vyb/parser/parser.hpp" // Corrected path
#include "vyb/parser/ast.hpp"
#include "vyb/parser/token.hpp"  // Corrected path
#include "vyb/semantic.hpp" // Added to make SemanticAnalyzer available
#include "vyb/vre/llvm/codegen.hpp" // Added for LLVMCodegen
#include "vyb/driver.hpp" // Added for vyb::Driver

// Declare function for JIT compilation and execution of Vyb code
// This is implemented in main.cpp with auto-serialization support
int run_vyb_code(const std::string& source, const std::string& fileName, bool generateLLVMIR = false);

/* // EBNF Grammar of the Vyb Language  // Uncommented
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
                         | bind_declaration
                         | function_declaration
                         | variable_declaration
                         | constant_declaration
                         | type_alias_declaration
                         | aspect_declaration // Added for aspect Comparable like constructs
                         | statement // Allow top-level statements like expressions for scripting or simple files

// Top-level declarations
import_statement       ::= 'import' path [ 'as' IDENTIFIER ] [';'] // Semicolon made optional
smuggle_statement      ::= 'smuggle' path [ 'as' IDENTIFIER ] [';'] // Semicolon made optional
path                   ::= IDENTIFIER { ('::' | '.') IDENTIFIER } // Allow . as path separator too

class_declaration      ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'class' IDENTIFIER [ 'extends' type ] [ 'implements' type_list ] '{' { class_member } '}'
class_member           ::= field_declaration | method_declaration | constructor_declaration
field_declaration      ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';'] // Unified field<Type> syntax
method_declaration     ::= [ 'pub' ] [ 'static' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ] IDENTIFIER '(' [ parameter_list ] ')' '<' type '>' '->' ( block_statement | expression [';'] ) [ 'throws' type_list ] // Unified method(params)<ReturnType> -> syntax
constructor_declaration::= [ 'pub' ] 'new' [ template_parameters ] '(' [ parameter_list ] ')' [ 'throws' type_list ] ( block_statement | '=>' expression [';'] ) // Added throws, expression body

struct_declaration     ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'struct' IDENTIFIER '{' { struct_field_declaration } '}'
struct_field_declaration ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';'] // Unified field<Type> syntax

enum_declaration       ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] 'enum' IDENTIFIER '{' { enum_variant } '}'
enum_variant           ::= IDENTIFIER [ '(' type_list ')' ] [ '=' expression ] ','?

bind_declaration       ::= [ 'template' '<' type_parameter_list '>' ] 'bind' type [ '->' type ] '{' { method_declaration } '}'

function_declaration   ::= [ 'pub' ] [ 'template' '<' type_parameter_list '>' ] [ 'async' ] IDENTIFIER '(' [ parameter_list ] ')' '<' type_list '>' '->' ( block_statement | expression [';'] | statement ) [ 'throws' type_list ] // Unified function(params)<ReturnType1, ReturnType2, ...> -> syntax

aspect_declaration     ::= [ 'pub' ] 'aspect' IDENTIFIER [ template_parameters ] '{' { method_signature } '}' // For aspect Comparable
method_signature       ::= [ 'async' ] IDENTIFIER '(' [ parameter_list ] ')' '<' type_list '>' '->' ';' [ 'throws' type_list ] // Unified method(params)<ReturnType1, ReturnType2, ...> -> syntax


variable_declaration   ::= [ 'pub' ] IDENTIFIER '<' type '>' [ '=' expression ] [';'] // Unified variable<Type> syntax
constant_declaration   ::= [ 'pub' ] 'const' IDENTIFIER '<' type '>' [ '=' expression ] [';'] // Unified const variable<Type> syntax

type_alias_declaration ::= [ 'pub' ] 'type' IDENTIFIER [ template_parameters ] '=' type [';'] // Semicolon optional


// Parameters and Type Lists
type_parameter_list    ::= type_parameter { ',' type_parameter }
type_parameter         ::= IDENTIFIER [ ':' type_bounds ] | expression // Allow expressions for const generics
type_bounds            ::= type { '+' type }
template_parameters    ::= '<' type_parameter_list '>'

parameter_list         ::= parameter { ',' parameter }
parameter              ::= [ 'const' ] IDENTIFIER '<' type '>' [ '=' expression ] // Unified parameter<Type> syntax

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
                         | pass_statement
                         | defer_statement
                         | try_statement
                         | variable_declaration
                         | constant_declaration
                         | pattern_assignment_statement // For ref x = _
                         | scoped_statement // For scoped { ... }
                         | throw_statement // Added

expression_statement   ::= expression [';'] // Semicolon made optional
block_statement        ::= '{' { statement } '}'

if_statement           ::= 'if' expression ( block_statement | statement_without_block ) { 'else' 'if' expression ( block_statement | statement_without_block ) } [ 'else' ( block_statement | statement_without_block ) ]
statement_without_block ::= expression_statement | return_statement | break_statement | continue_statement | throw_statement // etc., any statement not ending in a block itself.

for_statement          ::= 'for' pattern 'in' expression block_statement
while_statement        ::= 'while' expression block_statement // Consider also: while_let_statement
loop_statement         ::= 'loop' block_statement
match_statement        ::= 'match' '(' expression ')' '{' match_arm* '}'
match_arm              ::= pattern '->' ( expression | block_statement | statement_without_block ) ','?

select_expression      ::= 'select' '(' expression ')' '->' '{' select_arm* '}' ';'
select_arm             ::= pattern '->' ( expression | block_statement ) ','?

pattern                ::= comparison_pattern
                         | IDENTIFIER [ '@' pattern ]
                         | literal
                         | '?'                   // wildcard (no-match continues as NOP)
                         | path '{' [ field_pattern { ',' field_pattern } [','] ] '}' // Struct pattern
                         | path '(' [ pattern_list ] ')' // Enum variant pattern
                         | '[' [ pattern_list ] ']' // Array/slice pattern
                         | '(' pattern_list ')' // Tuple pattern
                         | '&' [ 'const' ] pattern // For reference patterns

comparison_pattern     ::= ( '==' | '!=' | '<' | '<=' | '>' | '>=' ) expression
                         // Comparison patterns for match/select (e.g., >= 90, < 60)
                         // Pattern evaluation: matched_value op pattern_value
                         // Unreachable patterns detected at compile time

field_pattern          ::= IDENTIFIER ':' pattern | IDENTIFIER
pattern_list           ::= pattern { ',' pattern }
pattern_assignment_statement ::= pattern '=' expression [';'] // For ref x = _

return_statement       ::= 'return' [ expression ] [';'] // Semicolon made optional
break_statement        ::= 'break' [ IDENTIFIER ] [ expression ] [';'] // Semicolon optional
continue_statement     ::= 'continue' [ IDENTIFIER ] [';'] // Semicolon optional
pass_statement         ::= 'pass' expression [';'] // Returns from select block, not enclosing function
defer_statement        ::= 'defer' ( expression_statement | block_statement ) // Semicolon handled by expr_stmt
try_statement          ::= 'try' block_statement { catch_clause } [ 'finally' block_statement ]
catch_clause           ::= 'catch' [ '(' IDENTIFIER ':' type ')' | IDENTIFIER ] block_statement // Adjusted for (e: Type)
scoped_statement       ::= 'scoped' block_statement // Added
throw_statement        ::= 'throw' expression [';'] // Added

// Expressions
expression             ::= assignment_expression
                           | BorrowExpr // Added BorrowExpr to the expression hierarchy

assignment_expression  ::= conditional_expression [ assignment_operator assignment_expression ]
assignment_operator    ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>='

conditional_expression ::= logical_or_expression [ '?' expression ':' conditional_expression ] // Ternary operator
                         | if_expression // Added if as expression

logical_or_expression  ::= logical_and_expression { '||' logical_and_expression }
logical_and_expression ::= bitwise_or_expression { '&&' bitwise_or_expression }
bitwise_or_expression  ::= bitwise_xor_expression { '|' bitwise_xor_expression }
bitwise_xor_expression ::= bitwise_and_expression { '^' bitwise_and_expression }
bitwise_and_expression ::= equality_expression { '&' equality_expression }
equality_expression    ::= relational_expression { ( '==' | '!=' ) relational_expression }
relational_expression  ::= range_expression { ( '<' | '<=' | '>' | '>=' | 'is' | 'as' ) range_expression } // Changed to range_expression
range_expression       ::= shift_expression [ '..' shift_expression ] // Added range expression x..y
shift_expression       ::= additive_expression { ( '<<' | '>>' ) additive_expression }
additive_expression    ::= multiplicative_expression { ( '+' | '-' ) multiplicative_expression }
multiplicative_expression ::= unary_expression { ( '*' | '/' | '%' ) unary_expression }

unary_expression       ::= ( '!' | '-' | '+' | '*' | 'await' | 'throw' ) unary_expression // Removed '&' and '&const' as they are part of Type or handled by BorrowExpr
                         | primary_expression

primary_expression     ::= literal
                         | path_expression
                         | '(' expression ')'
                         | call_expression          // If callee is a type (path_expression), yields ConstructionExpression
                         | member_access_expression
                         | index_access_expression
                         | list_comprehension
                         | array_literal            // For [elem1, elem2] and [elem; count]
                         | array_construction       // For [Type; Size]() for default initialization
                         | tuple_literal
                         | struct_literal           // For TypeName{...} and anonymous {...}
                         | lambda_expression
                         | select_expression        // Expression-based pattern matching
                         | 'self' | 'super'
                         // if_expression is now part of conditional_expression

if_expression          ::= 'if' expression block_statement 'else' ( block_statement | if_expression ) // For if used as expression

literal                ::= INTEGER_LITERAL | FLOAT_LITERAL | STRING_LITERAL | BOOLEAN_LITERAL | 'null'

path_expression        ::= path [ type_arguments ]

call_expression        ::= primary_expression '(' [ argument_list ] ')' [ '?' ] // Consider array_type() here
argument_list          ::= expression { ',' expression }

member_access_expression ::= primary_expression ( '.' | '?.' | '::' ) IDENTIFIER // Added :: for static-like access on instances if needed, or path handles it
index_access_expression  ::= primary_expression '[' expression ']' [ '?' ]

list_comprehension     ::= '[' expression 'for' pattern 'in' expression [ 'if' expression ] ']'
array_literal          ::= '[' [ expression { ',' expression } [','] ] ']'  // e.g., [elem1, elem2, ...]
                         | '[' expression ';' expression ']'                // e.g., [value; count]
array_construction     ::= ArrayType '(' ')' // Default initialization, e.g., [Int; 10]()

tuple_literal          ::= '(' [ expression { ',' expression } [ ',' ] ] ')'

struct_literal         ::= [ path_expression ] '{' [ struct_literal_field { ',' struct_literal_field } [ ',' ] ] '}' // Allows TypeName{...} and anonymous {...}
struct_literal_field   ::= IDENTIFIER (':' | '=') expression | IDENTIFIER // Allow = for field init

lambda_expression      ::= [ 'async' ] ( '|' [ parameter_list ] '|' | IDENTIFIER ) '<' type '>' ( '=>' expression | block_statement )

// Type System (NEW - based on mem_RFC.md)
Type                   ::= BaseType [ 'const' ] [ '?' ] // Added optional '?' for nullable types from old EBNF
BaseType               ::= IDENTIFIER // Represents a simple type name like Int, Foo
                         | OwnershipWrapper '<' Type '>'
                         | ArrayType
                         | TupleType
                         | FunctionType
                         | '(' Type ')' // Grouped type

OwnershipWrapper       ::= 'my' | 'our' | 'their' | 'ptr'

ArrayType              ::= '[' Type [ ';' Expression ] ']' // e.g., [Int], [Int; 5]
TupleType              ::= '(' [ Type { ',' Type } [ ',' ] ] ')' // e.g., (Int, String)
FunctionType           ::= 'fn' '(' [ Type { ',' Type } ] ')' [ '->' Type ] // e.g., fn(Int) -> Int; ommitted return is void (see type_parser.cpp)

// Path and Arguments (mostly from old EBNF, ensure compatibility)
path                   ::= IDENTIFIER { ('::' | '.') IDENTIFIER }
type_arguments         ::= '<' type_argument_list '>'
type_argument_list     ::= type_argument { ',' type_argument }
type_argument          ::= Type | Expression // For const generics

// Borrowing Intrinsics (shorthand calls)
// Parsed as regular call_expressions for borrow/view
BorrowExpr             ::= 'borrow' '(' Expression ')'
                         | 'view' '(' Expression ')'


// Note: The following old type productions are replaced or integrated above:
// core_type              ::= named_type ...
// named_type             ::= path [ type_arguments ] (now IDENTIFIER or path within BaseType, arguments via generic syntax if needed)
// array_type             ::= '[' type ( ';' expression )? ']' (now ArrayType)
// slice_type             ::= '[' type ']' (covered by ArrayType with no size expression)
// tuple_type             ::= '(' [ type_list [ ',' ] ] ')' (now TupleType)
// function_type          ::= [ 'async' ] 'fn' '(' [ type_list ] ')' [ '->' type ] [ 'throws' type_list ] (now FunctionType)
// reference_type         ::= '&' [ lifetime ] [ 'const' ] type (Replaced by 'their<Type>' and 'their<Type const>')
// pointer_type           ::= '*' ( 'const' | 'mut' ) type (Replaced by 'ptr<Type>' and 'ptr<Type const>')
// lifetime               ::= APOSTROPHE IDENTIFIER (Lifetimes are not in the current mem_RFC.md focus, can be added later if needed)

// End of EBNF Grammar
//
// General Notes:
// - Semicolon optionality: Many semicolons have been made optional ([';']) to align with examples.
//   The parser's actual strictness should be confirmed. If truly optional, it can lead to ambiguities.
// - Expression bodies for functions: `=> expression` added.
// - Unified Syntax: All declarations use `name<Type>` syntax consistently.
// - `if-expression`: Added to `conditional_expression`.
// - `statement_without_block`: Helper for `if` statements with non-block consequents.
// - Constructor calls: A `call_expression` of the form `TypeName ( arguments )`, where `TypeName` is a `path_expression` resolving to a type, is parsed as a `ConstructionExpression`.
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
*/

#endif // VYB_HPP
