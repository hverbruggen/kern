# Markdown that stays editable

The `core/` directory contains `inline code`. Type `blah` here to create another span.
Source line breaks stay in place. **Strong**, *emphasis*, and [links](https://example.org) keep their syntax.

## Code

```cpp
const auto result = calculate(42);
if (result > 0) return result;
```

- [x] Keep source intact
- [ ] Reveal syntax where the caret is

## Mathematics

Inline $\frac{a_1}{\sqrt{b}}$ and $e^{i\pi}+1=0$.

$$
\begin{pmatrix} a & b \\ c & d \end{pmatrix}
$$

<div class="note">HTML remains editable source.</div>

| Item | Content |
| --- | --- |
| Code | `a + b` |
| Emphasis | **Edit directly** |
