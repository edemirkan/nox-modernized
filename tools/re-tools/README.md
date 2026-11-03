Disassembly helpers for Game.exe (GOG 2000-07-21). First `python3 mkdis.py <Game.exe>` (makes dis.pkl here), then:
- `d.py ADDR [N]`       N instructions from ADDR (hex)
- `calls.py LO HI`      calls / jumps into functions between LO and HI, with their callers
- `fs.py ADDR...`       the function each address is in, and its callers
- `ptrs.py LO HI`       instructions that use a constant (data address) between LO and HI
