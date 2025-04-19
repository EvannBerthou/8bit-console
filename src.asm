// Macros

?+incr_reg
PSH @0
LDA @?0
ADD $1
SAR $?0
POP $0
?-

?+incr_reg_carry
PSH @0
LDA @?0
ADD @C
SAR $?0
POP $0
?-

// Code

LDA $0
SAR $3
SAR $6
SAR $7

outer_loop:
LDA $0
inner_loop:
PSH @0

//BODY
ADD @1
SAR $1

PXP @6,7
PXC @1

?incr_reg 7
?incr_reg_carry 6

POP $0

ADD $1
CMP $128
JNE inner_loop

?incr_reg 3

LDA @3
CMP $64
JNE outer_loop

// Infinite refresh
LDA $1
loop:
SAM $128,00
JAL loop
