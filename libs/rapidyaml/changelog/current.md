- Fix parsing of **valid** YAML corner cases:
  - Ensure tags/anchors are not omitted. This was happening in some flow maps ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)).
  - Ambiguity of tags/anchors in ? mode ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    ? &mapanchor
      key: val
    ?
      &keyanchor key: val
    ```
  - flow tags/anchors with omitted plain scalar ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    # ... likewise for !tag
    - [&anchor,&anchor]
    - {&anchor,&anchor}
    - [&anchor :,&anchor :]
    - {&anchor :,&anchor :}
    - [: &anchor,: &anchor]
    - {: &anchor,: &anchor}
    ---
    ? anchor
    ```
  - flow tags/anchors terminating with `:` (the colon is part of the tag/anchor) ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    # ... likewise for !tag:
    - [&anchor:,&anchor:]
    - {&anchor:,&anchor:}
    - [&anchor: :,&anchor: :]
    - {&anchor: :,&anchor: :}
    - [: &anchor:,: &anchor:]
    - {: &anchor:,: &anchor:}
    ---
    ? anchor
    ```
  - Fix corner cases of explicit keys now allow same-line containers ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    ? - a     # same-line container key now parses successfully. both seqs and maps
    : - b     # same-line container val now parses successfully. both seqs and maps
    ? ? - c   # nested explicit keys were also fixed
      ? - d
    ```
-----
- Ensure parse errors for **invalid** YAML cases, and improve reported error location:
  - colon on newline at top level ([PR#585](https://github.com/biojppm/rapidyaml/pull/585)):
    ```yaml
    scalar
    : bad
    ---
    [seq]
    : bad
    ---
    {map: }
    : bad
    ```
  - colon on newline generally in block containers ([PR#585](https://github.com/biojppm/rapidyaml/pull/585)):
    ```yaml
    bad cases:
      scalar
        : bad colon
      [seq]
        : bad colon
      {map: }
        : bad colon
    ```
  - colon on newline in flow sequences ([PR#586](https://github.com/biojppm/rapidyaml/pull/586)):
    ```yaml
    [a
      : 
      b]
    ```
  - tokens after explicit document end ([PR#585](https://github.com/biojppm/rapidyaml/pull/585)):
    ```yaml
    foo: bar
    ... bad tokens
    ```
  - `-` is invalid scalar in flow sequences ([PR#586](https://github.com/biojppm/rapidyaml/pull/586)):
    ```yaml
    [-]
    ---
    [-,-]
    ---
    [--,-]
    ---
    [-
     ]
    ```
  - doc start/begin tokens at zero indentation in seq flow and quoted scalars ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    [
    ---,
    --- ,
    ---\t,
    ...,
    ... ,
    ...\t,
    # etc
    ]
    ---
    "
    --- # error as well
    ... # error as well
    "
    ---
    '
    --- # error as well
    ... # error as well
    '
    ```
  - nested flow containers now enforce the contextual parent indentation ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    - - - [
        a  # now this is a parse error
         ]
    - - - [
         a  # this is ok
         ]
    ```
  - single/double-quoted scalars now enforce the contextual parent indentation ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    - - - "a
        b"  # now this is a parse error
    - - - "a
         b"  # this is ok
    ```
  - plain scalars in block mode starting with `,` ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    all invalid:
      - , foo
      - ,foo
      - ,
    ```
  - references with anchors or tags ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    all invalid:
      - &anchor *ref
      - !tag *ref
    ```
  - references with extra tokens ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    %YAML 1.2 this is wrong
    ```
  - multiline implicit keys are invalid ([PR#587](https://github.com/biojppm/rapidyaml/pull/587)):
    ```yaml
    multiline
        plain: invalid
    'multiline
        squoted': invalid
    "multiline
        dquoted": invalid
    [multiline,
        seq]: invalid
    {multiline:
        map}: invalid
   ```
