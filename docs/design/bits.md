# Guard, Round, and Sticky Bits

1.XXXXXXXXXXXXXXXXXXXXXXX G R S
  ^----------------------^ ^ ^ ^
  24-bit mantissa          | | |
                           | | Sticky bit
                           | Round bit  
                           Guard bit
```

### **Guard Bit (G)**

The guard bit is just an extra bit of precision that is used in calculations - it's the first bit position immediately after the mantissa .

**Definition:** The first bit beyond the mantissa precision. It behaves like a normal bit during shifts and operations.

### **Round Bit (R)**

The round bit is the second extra bit of precision used in calculations - it's the second bit position after the mantissa .

**Definition:** The second bit beyond the mantissa precision. It also behaves like a normal bit during shifts.

### **Sticky Bit (S)** - This one is special!

The sticky bit is an indication of what is/could be in lesser significant bits that are not kept. If a value of 1 ever is shifted into the sticky bit position, that sticky bit remains a 1 ("sticks" at 1), despite further shifts .

**Definition:** The sticky bit is a logical OR of all bits beyond the round bit position. Once it becomes 1, it stays 1.

## How the Sticky Bit Works - Example

When the mantissa shifts right, bits shift into the guard, round, and sticky positions normally, but once ANY 1 bit shifts into the sticky position, it stays 1 from that point on .

Here's a concrete example of shifting (from the sources):
```
                            G R S
Before shifts:  1.11000000000000000000100 0 0 0
After 1 shift:  0.11100000000000000000010 0 0 0
After 2 shifts: 0.01110000000000000000001 0 0 0
After 3 shifts: 0.00111000000000000000000 1 0 0  (bit enters G)
After 4 shifts: 0.00011100000000000000000 0 1 0  (bit enters R)
After 5 shifts: 0.00001110000000000000000 0 0 1  (bit enters S, becomes 1)
After 6 shifts: 0.00000111000000000000000 0 0 1  (S stays 1!)
After 7 shifts: 0.00000011100000000000000 0 0 1  (S stays 1!)
After 8 shifts: 0.00000001110000000000000 0 0 1  (S stays 1!)

## How They're Used for Rounding (Round-to-Nearest-Even)

The three bits together tell you:

G=0, R=x, S=x: Less than halfway → Round down
G=1, R=0, S=0: Exactly halfway → Round to even (look at LSB of mantissa)
G=1, R=1, S=x: More than halfway → Round up
G=1, R=0, S=1: More than halfway → Round up

The sticky bit distinguishes between "exactly 0.5" and "more than 0.5" when rounding.
Summary: Guard and round are normal bits. Sticky is special - it's the OR of everything beyond the round bit, and once set to 1, it stays 1.
