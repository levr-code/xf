# Slower xf interpreter on python
import json
import copy
import sys

def parse(text: str, sep: str = ",") -> list[str]:
    """
    Split `text` by `sep` but ignore separators inside:
      - single or double quotes
      - blocks `( ... )` outside quotes
    Handles escaped quotes and separators.
    """
    result = []
    buf = []
    depth = 0
    in_quote = None
    escape = False

    for c in text:
        if escape:
            buf.append(c)
            escape = False
            continue

        if c == "\\":
            buf.append(c)
            escape = True
            continue

        # start or end quote
        if in_quote:
            buf.append(c)
            if c == in_quote:
                in_quote = None
            continue
        elif c in ("'", '"'):
            buf.append(c)
            in_quote = c
            continue

        # handle blocks only outside quotes
        if c in "[{(" and not in_quote:
            depth += 1
            buf.append(c)
            continue
        if c in ")}]" and not in_quote:
            depth -= 1
            buf.append(c)
            continue

        # split only if outside quotes and blocks
        if c == sep and depth == 0 and not in_quote:
            result.append("".join(buf).strip())
            buf = []
        else:
            buf.append(c)

    if buf:
        result.append("".join(buf).strip())

    return result


class ReturnValue(Exception):
    def __init__(self, value, message="return outside a function!"):
        super().__init__(message)
        self.value = value
 

class Environment:

    def __init__(self):
        self.data = []
        self.refs = []
        self.free = []
        self.vars = {}

    def alloc(self, value):
        if self.free:
            addr = self.free.pop()
            self.data[addr] = value
            self.refs[addr] = 0
        else:
            addr = len(self.data)
            self.data.append(value)
            self.refs.append(0)
        return addr

    def set_var(self, name: str, value):
        if not isinstance(value, ALink):
            self.vars[str(name)] = ALink(self.alloc(value), self)
        else:
            self.vars[str(name)] = value
            self.data[value.as_addr()] = value.as_literal()
        return self.vars[str(name)]

    def get_var(self, name: str):
        if type(name) is not str:
            return name
        return ALink(self.vars[name].as_addr(), self)

    def get_heap(self, addr: int):
        return self.data[addr]

    def inc(self, addr: int):
        self.refs[addr] += 1

    def __repr__(self):
        return "Environment()"

    def dec(self, addr: int):
        self.refs[addr] -= 1
        if self.refs[addr] <= 0:
            self.data[addr] = None
            self.free.append(addr)


class BaseNode:
    pass


class ALink:

    def __init__(self, addr, env):
        self.__env = env
        self.__addr = addr
        env.inc(addr)

    def __bool__(self):
        return bool(self.as_literal())

    def as_addr(self):
        return self.__addr

    def as_literal(self):
        return self.__env.get_heap(self.__addr)

    def __del__(self):
        try:
            self.__env.dec(self.__addr)
        except (AttributeError, TypeError, ReferenceError):  # PYTHON 3.14 !!!
            pass

    @property
    def env(self):
        return self.__env

    def __str__(self):
        return str(self.as_literal())

    def __repr__(self):
        return str(self.as_literal())


class Node(BaseNode):

    def __init__(self, args, env):
        self.args = args
        self.env = env

    def __str__(self):
        return str([self.env, self.args])


class Literal(BaseNode):

    def __init__(self, val=None, env=None):
        self._val = val
        self._env = env

    def __repr__(self):
        return f"Literal({[self._val, self._env]})"

    def __str__(self):
        return str([self._val, self._env])

    def run(self):
        return self._val


class Bool(Literal):
    pass


class Link(Literal):
    def run(self):
        return ALink(self._val, self._env)


class BlockNode(Literal):
    def __init__(self, val=None, env=None):
        super().__init__(val, env)
        self.evalled = ast1(self._val, self._env)
    def run(self):
        return self.evalled


class Code(Node):
    """the main executor"""
    COMMANDS: dict = {}

    def __init__(self, name, args, env):
        super().__init__(args, env)
        self.name = name

    def __repr__(self):
        return f"Code({repr(self.name)}, {self.args}, {self.env})"

    @classmethod
    def register(cls, as_what):
        def decor(func):
            cls.COMMANDS[as_what] = func
            return func
        return decor

    def run(self):
        if self.name not in self.COMMANDS:
            return ALink(
                self.env.alloc([self.name, *[i.run() for i in self.args]]),
                self.env)
        return self.COMMANDS[self.name](env=self.env,
                                        args=[i.run() for i in self.args])


class Program:
    def __init__(self, values: list[Code]):
        self._values = values

    def __str__(self):
        return str(self._values)

    def __repr__(self):
        return f"Program({self._values})"

    def run(self):
        try:
            results = [i.run() for i in self._values]
            return results[-1] if results else None
        except ReturnValue as ret:
            return ret.value


def convert_to_links(a, env):
    if isinstance(a, str):
        return ALink(env.alloc(a), env)
    if isinstance(a, list):
        for i, val in enumerate(a):
            a[i] = convert_to_links(val, env)
        return ALink(env.alloc(a), env)
    if isinstance(a, dict):
        for i, val in a.values():
            a[i] = convert_to_links(val, env)
    return a
    


def ast1(code, env: Environment): # not ai, all those comments are mine

    program = []
    for j in parse(code, "\n"):  # iteratin throw splitted by logical newlines code
        sp = j.split(':')  # splitting by the : to get the body and the arg(all the sp[1:] will be resplitted)
        name = sp[0]  # getting the command
        args = parse(":".join(sp[1:]), ',')  # resplitting the args
        for i, arg in enumerate(args):
            if arg.startswith("${") and arg[-1] == '}':  # the dinamic placeholder substitution
                args[i] = ast1(arg[2:-1], env)  # calling the full ast on the code part(in python it is something like (lambda: something)())
            elif arg.startswith(('{', '[')) and arg[-1] in {'}', ']'}:  # startswith is a safe way to check does the line start with something,
                # also it is slower, thats why i use arg[-1] 
                args[i] = Link(env.alloc(convert_to_links(json.loads(arg), env).as_literal()), env)  # Link is made because convert_to_links returns an ALink
                # ALink is an already evaluated Link, so for it to work we convert it to not evaluated stage
            elif arg.isdigit():  # basic int handeling, to create floats you should use division
                args[i] = Literal(int(arg))
            elif arg == 'null':  # null is None in JS(shorthand for JavaScript)
                args[i] = Literal()  # the placeholders for env and val are already None so no need to give anything
            elif arg.startswith(('"', "'")) and arg[-1] in {'"', "'"}: # this is the string handler it handles the string literals
                args[i] = Link(env.alloc(arg[1:-1]), env)  # env. alloc returns an addres in data list where the "allocated" thing is located
            elif arg in {'true', 'false'}: # some basic boolean handeling
                args[i] = Bool(arg == 'true')
            elif arg.startswith('(') and arg[-1] == ')': # BlockNode when evaluated becames a Program (the root node) this allows lambdas also set = def
                args[i] = BlockNode(arg[1:-1], env)  # AAAA
            else:
                raise NotImplementedError((f"{arg} at position {i}"
                                           "can't be of any known type")) 
        program.append(Code(name, args, env))
    return Program(program)


def real_value(vl):
    if isinstance(vl, ALink):
        return vl.as_literal()
    elif issubclass(vl, Node):
        return real_value(vl.run())
    else:
        return vl


@Code.register('op')
def add(env: Environment, args):
    try:
        a = real_value(args[0])
        b = real_value(args[2])
    except IndexError:
        pass
    match args[1]:
        case '*':
            return a*b
        case '+':
            return a + b
        case '-':
            return a - b
        case '/':
            return a / b
        case '&':
            return a & b
        case '&&':
            return a and b
        case '|':
            return a | b
        case '||':
            return a or b
        case '%':
            return a % b
        case '^':
            return a ^ b
        case '$':
            return env.get_var(a)
        case 'copy':
            return env.set_var(env.get_var(a))


@Code.register('echo')
def echo(env, args):
    print(*args)


@Code.register('def')
def def_func(env, args):
    func_body_str = args[1]
    env.set_var(args[0].as_literal(), func_body_str)


def func_prepare(ast, old, new, env):
    if isinstance(ast, Program):
        for i in ast._values:
            func_prepare(i, old, new, env)
    elif isinstance(ast, Code):
        ast.env = env
        for i in ast.args:
            func_prepare(i, old, new, env)
    elif issubclass(type(ast), Literal):
        ast._env = env
        if ast._val == old:
            ast._val = new
    if isinstance(ast, BlockNode):
        func_prepare(ast._val, old,  new, env)


@Code.register('input')
def io_input(env, args):
    return input(*args)


@Code.register('call')
def call_func(env, args):
    func_link = env.get_var(args[0].as_literal())
    body_ast = func_link.as_literal()

    body_ast = copy.deepcopy(body_ast)

    for i, arg in enumerate(args):
        func_prepare(body_ast, f"&{{{i}}}", arg, env)
    return body_ast.run()


@Code.register('gt_ind')
def get_index(env, args):
    return args[0].as_literal()[args[1]]


@Code.register('st_ind')
def set_index(env: Environment, args):
    env.data[args[0].as_addr()][args[1]] = args[2]
    return args[2]



@Code.register('get')
def get_var(env, args):
    return env.get_var(args[0].as_literal())


@Code.register('set')
def set_var(env, args):
    return env.set_var(args[0].as_literal(), args[1])


@Code.register('if')
def if_(env, args):
    if args[0]:
        return args[1].run()
    if len(args) == 3:
        return args[2].run()


@Code.register('return')
def return_(env, args):
    raise ReturnValue(args[0])


@Code.register('while')
def while_(env, args):
    while args[0].run():
        args[1].run()


e = Environment()
with open(sys.argv[1].strip(), 'r', encoding='utf-8') as theFile:
    a = ast1(theFile.read(), e)
print(a.run())
