# Penelope Test Document

This is a **test document** for *Penelope* M2. It exercises all the major
markdown elements that the DocumentBuilder should handle.

## Headings

### Third Level Heading

#### Fourth Level Heading

##### Fifth Level Heading

###### Sixth Level Heading

## Text Formatting

This paragraph has **bold text**, *italic text*, ***bold and italic***,
~~strikethrough~~, and `inline code`. Here is a [link to example](https://example.com "Example Site").

## Block Quotes

> This is a block quote. It should appear indented and in italics.
>
> > This is a nested block quote.

## Lists

### Unordered List

- First item
- Second item
  - Nested item one
  - Nested item two
    - Deeply nested
- Third item

### Ordered List

1. First ordered item
2. Second ordered item
3. Third ordered item

### Task List

- [x] Completed task
- [ ] Incomplete task
- [x] Another completed task

## Code Blocks

Inline code: `printf("hello world")`

Fenced code block:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello from Penelope!" << std::endl;
    return 0;
}
```

```python
def greet(name):
    """Greet someone by name."""
    print(f"Hello, {name}!")

greet("World")
```

## Tables

| Feature | Status | Priority |
|---------|:------:|-------:|
| Headings | Done | High |
| Bold/Italic | Done | High |
| Code Blocks | Done | High |
| Tables | Done | Medium |
| Images | Done | Medium |

## Horizontal Rule

---

## HTML Entities

Copyright &copy; 2026 &mdash; All rights reserved &trade;

## Special Characters

Curly quotes: &ldquo;Hello&rdquo; and &lsquo;World&rsquo;

Ellipsis: And then&hellip;

## Conclusion

This document tests the core rendering pipeline of **Penelope** M2.
If all elements above are properly formatted, the DocumentBuilder is working
correctly.
