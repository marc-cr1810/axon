# Axon C++ Code Style Guide

This guide describes the conventions and best practices for writing C++ code in the Axon project, based on analysis of the project's `.cpp` and `.hpp` files.

---

## File Structure

- **Header Files (`.hpp`):**
  - Use `#pragma once` at the top.
  - Group related class, struct, and function declarations.
  - Prefer forward declarations to reduce dependencies.
  - Place public APIs before private/protected members.

- **Source Files (`.cpp`):**
  - Include the corresponding header first.
  - Group includes: standard library, then project headers.
  - Keep implementation details out of headers.

---

## Naming Conventions

- **Namespaces:** Use lowercase (e.g., `namespace axon`).
- **Classes/Structs:** Use `snake_case` with `_t` suffix (e.g., `class parser_t`, `struct parameter_t`).
- **Functions/Methods:** Use `snake_case` (e.g., `get_next_token`, `add_member_variable`).
- **Variables:** Use `snake_case` (e.g., `current_token`, `m_position`, `method_name`).
- **Member Variables:** Prefix with `m_` (e.g., `m_line`, `m_body`, `m_methods`).
- **Static Variables:** Prefix with `s_` (e.g., `s_builtin_functions`, `s_builtins_initialized`).
- **Global Variables:** Prefix with `g_` (e.g., `g_config`, `g_logger`).
- **Enums:** Use `snake_case` with `_e` suffix (e.g., `enum parser_type_e`).
- **Constants:** Use all uppercase with underscores (e.g., `MAX_SIZE`).

---

## Formatting

- **Indentation:** Use 2 spaces per level. No tabs.
- **Braces:** Place opening braces on a new line for all control statements (`if`, `for`, `while`, `switch`, `class`, `struct`, etc.).
  ```cpp
  if (condition)
  {
    // code
  }

  for (int i = 0; i < 10; ++i)
  {
    // code
  }

  while (running)
  {
    // code
  }

  class example_class_t
  {
    // code
  }
  ```
- **Line Length:** Prefer lines under 240 characters.
- **Spacing:** Use spaces around operators and after commas.
  ```cpp
  int sum = a + b;
  func(arg1, arg2);
  ```

---

## Classes and Structs

- **Access Specifiers:** Group members by `public`, then `protected`, then `private`.
- **Within each access group, list functions before variables.**
- **Member Variables:** Member variables should be `private` or `protected` to enforce encapsulation.
- **Getters and Setters:** Provide public getter and setter methods for accessing and modifying member variables.
- **Constructors:** Use initializer lists for member variables.
- **Destructors:** Use `virtual` destructors for base classes.
- **Member Functions:** Mark as `const` if they do not modify the object.

---

## Example Header and Source File

**Header File Example (`example_class.hpp`):**

```cpp
#pragma once

#include <string>

namespace axon
{

class example_class_t
{
public:
  // Functions
  example_class_t(const std::string& name, int value);
  auto get_name() const -> const std::string&;
  auto set_name(const std::string& name) -> void;
  auto get_value() const -> int;
  auto set_value(int value) -> void;
  auto print_info() const -> void;

private:
  // Variables
  std::string m_name;
  int m_value;
};

} // namespace axon
```

**Source File Example (`example_class.cpp`):**

```cpp
#include "example_class.hpp"
#include <iostream>

namespace axon
{

example_class_t::example_class_t(const std::string& name, int value)
  : m_name(name), m_value(value)
{
}

auto example_class_t::get_name() const -> const std::string&
{
  return m_name;
}

auto example_class_t::set_name(const std::string& name) -> void
{
  m_name = name;
}

auto example_class_t::get_value() const -> int
{
  return m_value;
}

auto example_class_t::set_value(int value) -> void
{
  m_value = value;
}

auto example_class_t::print_info() const -> void
{
  std::cout << "Name: " << m_name << ", Value: " << m_value << std::endl;
}

} // namespace axon
```

---

## Summary

Follow these conventions for all C++ code in Axon to ensure consistency, readability, and maintainability.
