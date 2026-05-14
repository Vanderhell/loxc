# File Format

## Endianness

All multi-byte fields are little-endian.

## `.loxctab`

Portable table blob used by the runtime loader.

### Header: 24 bytes

| Offset | Size | Field |
|--------:|-----:|-------|
| 0..3    | 4    | Magic `LOXT` |
| 4       | 1    | Version (`1`) |
| 5       | 1    | Strategy ID |
| 6       | 1    | Base size (`0`, `4`, or `8`) |
| 7       | 1    | Bits per level |
| 8..9    | 2    | Level count |
| 10..11  | 2    | Reserved |
| 12..15  | 4    | Symbol count |
| 16..19  | 4    | Dictionary count |
| 20..23  | 4    | Data size |

### Data section

The data section is `data_size` bytes long and contains:

1. `byte_to_symbol[256]`
   - 256 x `u32`
   - 1024 bytes total
2. `symbols[N]`
   - each entry is `u8 type` + `u32 byte_or_idx`
   - 5 bytes per symbol
3. `dict_offsets[D+1]`
   - `u32` offsets into `dict_data`
4. `dict_data_size`
   - `u32`
5. `dict_data[...]`
   - raw dictionary bytes

### Trailer

The last 4 bytes are CRC32 over `header + data`.

## `.loxc`

Container format used by compression output.

### Magic

The container begins with the ASCII magic `LXC`.

### Header

`loxc_header_t` is written as a packed 15-byte header when the CRC flag is not
set, or as a 19-byte header when the trailing CRC field is present:

| Offset | Size | Field |
|--------:|-----:|-------|
| 0..2    | 3    | Magic `LXC` |
| 3       | 1    | Module ID |
| 4       | 1    | Version |
| 5       | 1    | Flags |
| 6       | 1    | Strategy ID |
| 7..8    | 2    | Data length |
| 9..10   | 2    | Level count |
| 11..14  | 4    | Reserved |
| 15..18  | 4    | CRC32, present only when `LOXC_FLAG_CRC` is set |

### Payload

- external mode: header + encoded bitstream
- embedded mode: header + raw `.loxctab` blob + encoded bitstream

## CRC32

CRC32 is used as a trailing integrity check for `.loxctab` blobs and for `.loxc`
headers when `LOXC_FLAG_CRC` is set.

The implementation lives in `loxc_crc32()` and uses the project-wide polynomial
defined in `src/loxc_base.c`.
