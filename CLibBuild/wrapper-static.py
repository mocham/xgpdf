#!/usr/bin/env python3
import re
import sys
from typing import List, Dict, Tuple

def extract_functions_cpp(source: str) -> List[Tuple[str, str, List[str]]]:
    """Extract C-compatible functions from extern "C" blocks."""
    # First extract the extern "C" block if it exists
    extern_c_blocks = re.findall(r'extern\s+"C"\s*\{([^}]*)\}', source, re.DOTALL)
    if not extern_c_blocks:
        return []
    
    # Combine all extern "C" blocks
    extern_c_source = '\n'.join(extern_c_blocks)
    
    # Remove comments
    extern_c_source = re.sub(r'/\*.*?\*/', '', extern_c_source, flags=re.DOTALL)
    extern_c_source = re.sub(r'//.*$', '', extern_c_source, flags=re.MULTILINE)
    
    # Pattern to match function declarations
    pattern = r'^(?:extern\s+)??(void|void\*|char|uint32_t|uint32_t\*|size_t\*|size_t|char\*|int|int\*|float|float\*|double|double\*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)'
    matches = re.finditer(pattern, extern_c_source, re.MULTILINE)
    
    functions = []
    for match in matches:
        return_type = match.group(1).strip()
        func_name = match.group(2).strip()
        params = [p.strip() for p in match.group(3).split(',') if p.strip()]
        functions.append((return_type, func_name, params))
    
    return functions

def extract_functions(source: str) -> List[Tuple[str, str, List[str]]]:
    """Extract functions with specific return types at start of line."""
    def clean_line(line: str) -> str:
        """Remove comments from a single line while preserving function declarations."""
        # Remove // comments
        line = re.sub(r'//.*$', '', line)
        # Remove /* */ comments that are on the same line
        line = re.sub(r'/\*.*?\*/', '', line)
        return line.strip()

    lines = source.split('\n')
    processed_lines = []
    in_function = False
    brace_count = 0

    for line in lines:
        # Check if this line starts a function declaration
        if re.match(r'^\s*(void|char|uint8_t|uint32_t|size_t|int|float|double)', line) and '(' in line:
            in_function = True
            brace_count = 0
        
        if in_function:
            # Clean the line but preserve the structure
            cleaned_line = clean_line(line)
            processed_lines.append(cleaned_line)
            
            # Track braces to detect function end
            brace_count += line.count('{') - line.count('}')
            if brace_count <= 0 and '{' in line:
                in_function = False
        else:
            processed_lines.append(line)

    # Join the processed lines and extract functions
    processed_source = '\n'.join(processed_lines)
    
    pattern = r'^(void|void\*|char|uint32_t|uint32_t\*|size_t\*|size_t|char\*|int|int\*|float|float\*|double|double\*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)'
    matches = re.finditer(pattern, processed_source, re.MULTILINE)
    
    functions = []
    for match in matches:
        return_type = match.group(1).strip()
        func_name = match.group(2).strip()
        params = [p.strip() for p in match.group(3).split(',') if p.strip()]
        if f"static {return_type} {func_name}" in source:
            continue
        functions.append((return_type, func_name, params))
    
    return functions

def clean_param(param: str) -> str:
    """Remove parameter name while preserving type and qualifiers."""
    # Remove comments
    param = re.sub(r'/\*.*?\*/', '', param)
    # Split into tokens
    tokens = re.findall(r'\w+\*?|\*|\w+', param)
    # Filter out the parameter name (last token that's not a type qualifier)
    type_qualifiers = {'const', 'volatile', 'restrict', 'signed', 'unsigned'}
    type_tokens = []
    found_name = False
    
    # Process tokens in reverse to find the parameter name
    for token in reversed(tokens):
        if not found_name and token not in type_qualifiers and not token.startswith('*'):
            found_name = True
            continue
        type_tokens.insert(0, token)
    
    # Reconstruct the type
    return ' '.join(type_tokens)

def generate_wrapper_code(functions, soname: str) -> str:
    """Generate wrapper code from the given source."""
    libname = soname[:-3]  # Remove .so extension
    
    # Filter out internal functions (those starting with stbi_ or WebP)
    functions = [f for f in functions if not f[1].startswith(('stbi_', 'stbir_', 'WebP'))]
    
    # Generate header
    wrapper = ""
    
    # Generate wrapper functions
    for return_type, func_name, params in functions:
        param_list = ', '.join(params)
        param_names = ', '.join([p.split()[-1].replace('*', '').replace('[', '').replace(']', '') for p in params])
        
        wrapper += f"{return_type} {func_name}({param_list});\n"
    
    return wrapper.replace("PopplerDocument", "void")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <target.so> <target.h> <source_file.c>...", file=sys.stderr)
        sys.exit(1)

    functions = []
    for fn in sys.argv[3:]:
        try:
            with open(fn, 'r') as f:
                source = f.read()
                is_cpp = fn[-3:] == ".cc"
                if is_cpp:
                    functions.extend(extract_functions_cpp(source))
                else:
                    functions.extend(extract_functions(source))
        except IOError as e:
            print(f"Error reading file: {e}", file=sys.stderr)
            sys.exit(1)
    
    wrapper_code = generate_wrapper_code(functions, sys.argv[1])
    with open(sys.argv[2], "w") as f:
        f.write(wrapper_code)
