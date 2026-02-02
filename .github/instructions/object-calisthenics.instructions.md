---
applyTo: '**/*.{cs,ts,java}'
description: Enforces Object Calisthenics principles for business domain code to ensure clean, maintainable, and robust code
---
# Object Calisthenics Rules

> ⚠️ **Warning:** This file contains the 9 original Object Calisthenics rules. No additional rules must be added, and none of these rules should be replaced or removed.

## Objective
This rule enforces the principles of Object Calisthenics to ensure clean, maintainable, and robust code in the backend, **primarily for business domain code**.

## Scope and Application
- **Primary focus**: Business domain classes (aggregates, entities, value objects, domain services)
- **Secondary focus**: Application layer services and use case handlers
- **Exemptions**: 
  - DTOs (Data Transfer Objects)
  - API models/contracts
  - Configuration classes
  - Simple data containers without business logic
  - Infrastructure code where flexibility is needed

## Key Principles

1. **One Level of Indentation per Method**:
   - Ensure methods are simple and do not exceed one level of indentation.

2. **Don't Use the ELSE Keyword**:
   - Avoid using the `else` keyword to reduce complexity and improve readability.
   - Use early returns to handle conditions instead.
   - Use Fail Fast principle
   - Use Guard Clauses to validate inputs and conditions at the beginning of methods.

3. **Wrapping All Primitives and Strings**:
   - Avoid using primitive types directly in your code.
   - Wrap them in classes to provide meaningful context and behavior.

4. **First Class Collections**:
   - Use collections to encapsulate data and behavior, rather than exposing raw data structures.

5. **One Dot per Line**:
   - Avoid violating Law of Demeter by only having a single dot per line.

6. **Don't abbreviate**:
   - Use meaningful names for classes, methods, and variables.
   - Avoid abbreviations that can lead to confusion.

7. **Keep entities small (Class, method, namespace or package)**:
   - Limit the size of classes and methods to improve code readability and maintainability.
   - Each class should have a single responsibility and be as small as possible.
   
   Constraints:
   - Maximum 10 methods per class
   - Maximum 50 lines per class
   - Maximum 10 classes per package or namespace

8. **No Classes with More Than Two Instance Variables**:
   - Encourage classes to have a single responsibility by limiting the number of instance variables.
   - Limit the number of instance variables to two to maintain simplicity.
   - Do not count ILogger or any other logger as instance variable.

9. **No Getters/Setters in Domain Classes**:
   - Avoid exposing setters for properties in domain classes.
   - Use private constructors and static factory methods for object creation.
   - **Note**: This rule applies primarily to domain classes, not DTOs or data transfer objects.

## References
- [Object Calisthenics - Original 9 Rules by Jeff Bay](https://www.cs.helsinki.fi/u/luontola/tdd-2009/ext/ObjectCalisthenics.pdf)
