#@@CASE math_expr_precedence_bitwise_and_hex_output
#@@OUTCOME SUCCESS
#@@QUERY VAR M_PRECEDENCE
#@@QUERY VAR M_BITS
#@@QUERY VAR M_HEX
math(EXPR M_PRECEDENCE "2 + 3 * 4")
math(EXPR M_BITS "(1 << 5) | (6 & 3)")
math(EXPR M_HEX "((7 & 3) + (8 >> 1))" OUTPUT_FORMAT HEXADECIMAL)
#@@ENDCASE

#@@CASE math_invalid_invocation_shapes_error
#@@OUTCOME ERROR
math()
math(EXPR)
#@@ENDCASE
