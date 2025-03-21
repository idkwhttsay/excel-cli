# Excel-CLI


### Simple Spreadsheet Expression Parser

This program implements a basic expression parsing system for a spreadsheet-like application.

It supports operations such as numerical expressions, cell references, and addition operations.

The program reads a CSV file containing expressions and parses them into an expression tree.


```csv
Date      |Amount of A |Price of A|Sum         |Total |
17.07.2021|40.0        |     2.50 | =B1 * C1   |=D1   |
18.07.2021|70.24       |=C1+1     |   :^       |=E1+D2|
19.07.2021|65.5        |     :^   |   :^       |:^    |
20.07.2021|38.2        |     :^   |   :^       |:^    |
21.07.2021|50.0        |     :^   |   :^       |:^    |
```

And outputs:

```csv
Date       | Amount of A | Price of A | Sum        | Total      
17.07.2021 | 40.000000   | 2.500000   | 100.000000 | 100.000000 
18.07.2021 | 70.240000   | 3.500000   | 245.840000 | 345.840000 
19.07.2021 | 65.500000   | 4.500000   | 294.750000 | 640.590000 
20.07.2021 | 38.200000   | 5.500000   | 210.100000 | 850.690000 
21.07.2021 | 50.000000   | 6.500000   | 325.000000 | 1175.690000
```

Basically a simple Excel engine without any UI.

## Quick Start

The project is using [nob](https://github.com/tsoding/nob.h) build system.

```console
$ cc -o nob nob.c
$ ./nob
$ ./excel-cli csv/bills.csv
```

## Syntax

### Types of Cells

| Type       | Description                                                                                                        | Examples                          |
| ---        | ---                                                                                                                | ---                               |
| Text       | Just a human readiable text.                                                                                       | `A`, `B`, `C`, etc                |
| Number     | Anything that can be parsed as a double by [strtod](https://en.cppreference.com/w/c/string/byte/strtof)                                                                | `1`, `2.0`, `1e-6`, etc           |
| Expression | Always starts with `=`. Excel style math expression that involves numbers, binary operations, unary operations, and other cells.                         | `=A1+B1`, `=1+2`, `=A1+100` etc |
| Clone      | Always starts with `:`. Clones a neighbor cell in a particular direction denoted by characters `<`, `>`, `v`, `^`. | `:<`, `:>`, `:v`, `:^`             |
