# Blade

## Introduction

Blade is new tree-hashing function that is design for efficient processing of chunks, in a Merkle-tree like reduction process. It use BLAKE3 at its core and incorporate custom bit diffusion and mixing methods to ensure high avalanche effect.

**Important note:** Blade is released with absolutely no cryptographic guarantees at all. While tests has been ran to verify if the added diffusion methods impact BLAKE3's output quality, **Blade has not been verified by any independant expert in cryprtography and is released with experimental goal in mind.**

## Detailled Functionning

### `blade_core`

`blade_core` is the core reduction engine. It use BLAKE3 in his keyed mode. It process 64 bytes of input and compress them in 32 bytes of output. This work like this:

1) We first modify all the bytes of the input by 64 bits parts using this formula :

    `~(((temp<<(iteration_num))|(temp>>(64-iteration_num)))^chunk_size)`

    Here, `temp` is the 64 bits part of the input being changed. `iteration_num` is the amount of iteration inside the merkle tree structure and `chunk_size` is the size of the chunk being proceded.
    This ensure even higher diffusion. The result of the whole operation is stored inside a temporary buffer, `input_seed`.

2) Then, we hash `input_seed` with BLAKE3 in normal mode to obtain the 32 bytes ley that we put inside `seed`.

3) Finally, we use BLAKE3 in keyed mode with the original `input` as input and the `seed` as key. The 32-byte output of this keyed BLAKE3 hash is the final result of `blade_core`.

### `blade`

`blade` is the main function that do all the things related to padding, parallelism, merkle tree reduction, inter-iteration mixing and recursive call on itself. It work like this:

1) Firstly, it verify if the output buffer is exactly 64 bytes and that the input is at least 128 bytes.

2) Then, if the input size is 64 bytes, it return the input. It's important for the recursive call logic. This serves as the base case for the recursive reduction process.

3) Next, the input size is checked to be a multiple of 128 bytes. If not, padding is applied: the original input is hashed using BLAKE3 in normal mode to generate 128 bytes of padding material. This material is appended to the input until its size becomes a multiple of 128 bytes.

4) The padded input is then deterministically partitioned into a series of glouton chunks, each being a power of two size and greater than or equal to 128 bytes. This glouton decomposition is crucial for optimal parallel processing.

5) Each glouton chunk then undergoes a multi-iteration reduction process, where the `chunk_size` parameter for `blade_core` is derived from the current chunk's dimensions. For each chunk, the algorithm proceeds as follows:

    - First, the system determines whether to use parallel execution. Parallelism is activated if: more than one hardware thread is available, there is more than one 64-byte 'couple' to process, and the `padded_input_size` exceeds 128 kilobytes. This decision can be overridden by the global booleans `force_parallelism` (which forces parallelism regardless of size, overriding `block_parallelism`) or `block_parallelism` (which disables it entirely).

    - Second, the chunk reduction process begins. If parallelism is enabled, the 64-byte 'couples' are distributed nearly equally among the available threads for processing. Otherwise, all couples are processed serially by a single call to `blade_batch_serial`.

    - Third, after `blade_core` processing, an inter-iteration mixing step is applied to the concatenated results. This step is crucial for maximal diffusion. The concatenated 32-byte outputs are treated as pairs of 64-bit integers. The mixing formulas for each pair (`temp_first`, `temp_second`) are:

        `((temp_first<<rot_amount)|(temp_first>>(64-rot_amount)))^temp_second`

    
        And the formula for the new second one is:

        `((temp_second<<rot_amount)|(temp_second>>(64-rot_amount)))^temp_first`

        The second one is treated right after the second one. The `rot_amount` is determined with this formula:

        `(t*3+1)%64`

        Here, `t` is the number of repetition of this whole operation. The entire string of concatenated results is treated with an overlapping window that is defined with a increment of 12 bytes by each iteration on the entire string. And finally, there is 11 eleven rounds of this whole operation, ensuring maximum diffusion.

5) After each iteration, the chunk size is reduced by half his previous size. The `iteration_num` of `blade_core` is the number of iteration that have been achieved.

6) After the processing of every chunk, all the results of all the chunks are 64 bytes of lenght. They are concatenated and then reduced recursively by the `blade` function calling itself on the concatenated results.

## License and warning

Blade is published under the MIT license. It use BLAKE3 which is itself under the CC0 license.