import pytest

import env
from pybind11_tests import ConstructorStats
from pybind11_tests import modules as m
from pybind11_tests.modules import subsubmodule as ms


def test_nested_modules():
    import pybind11_tests

    assert pybind11_tests.__name__ == "pybind11_tests"
    assert pybind11_tests.modules.__name__ == "pybind11_tests.modules"
    assert (
        pybind11_tests.modules.subsubmodule.__name__
        == "pybind11_tests.modules.subsubmodule"
    )
    assert m.__name__ == "pybind11_tests.modules"
    assert ms.__name__ == "pybind11_tests.modules.subsubmodule"

    assert ms.submodule_func() == "submodule_func()"


def test_reference_internal():
    b = ms.B()
    assert str(b.get_a1()) == "A[1]"
    assert str(b.a1) == "A[1]"
    assert str(b.get_a2()) == "A[2]"
    assert str(b.a2) == "A[2]"

    b.a1 = ms.A(42)
    b.a2 = ms.A(43)
    assert str(b.get_a1()) == "A[42]"
    assert str(b.a1) == "A[42]"
    assert str(b.get_a2()) == "A[43]"
    assert str(b.a2) == "A[43]"

    astats, bstats = ConstructorStats.get(ms.A), ConstructorStats.get(ms.B)
    assert astats.alive() == 2
    assert bstats.alive() == 1
    del b
    assert astats.alive() == 0
    assert bstats.alive() == 0
    assert astats.values() == ["1", "2", "42", "43"]
    assert bstats.values() == []
    assert astats.default_constructions == 0
    assert bstats.default_constructions == 1
    assert astats.copy_constructions == 0
    assert bstats.copy_constructions == 0
    # assert astats.move_constructions >= 0  # Don't invoke any
    # assert bstats.move_constructions >= 0  # Don't invoke any
    assert astats.copy_assignments == 2
    assert bstats.copy_assignments == 0
    assert astats.move_assignments == 0
    assert bstats.move_assignments == 0


def test_importing():
    from collections import OrderedDict

    from pybind11_tests.modules import OD

    assert OD is OrderedDict
    assert str(OD([(1, "a"), (2, "b")])) == "OrderedDict([(1, 'a'), (2, 'b')])"


def test_pydoc():
    """Pydoc needs to be able to provide help() for everything inside a pybind11 module"""
    import pydoc

    import pybind11_tests

    assert pybind11_tests.__name__ == "pybind11_tests"
    assert pybind11_tests.__doc__ == "pybind11 test module"
    assert pydoc.text.docmodule(pybind11_tests)


def test_duplicate_registration():
    """Registering two things with the same name"""

    assert m.duplicate_registration() == []


def test_builtin_key_type():
    """Test that all the keys in the builtin modules have type str.

    Previous versions of pybind11 would add a unicode key in python 2.
    """
    if hasattr(__builtins__, "keys"):
        keys = __builtins__.keys()
    else:  # this is to make pypy happy since builtins is different there.
        keys = __builtins__.__dict__.keys()

    assert {type(k) for k in keys} == {str}


@pytest.mark.xfail("env.PYPY", reason="PyModule_GetName()")
def test_def_submodule_failures():
    sm = m.def_submodule(m, b"ScratchSubModuleName")  # Using bytes to show it works.
    assert sm.__name__ == m.__name__ + "." + "ScratchSubModuleName"
    malformed_utf8 = b"\x80"
    if env.PYPY:
        # It is not worth the effort finding a trigger for a failure when running with PyPy.
        pytest.skip("Sufficiently exercised on platforms other than PyPy.")
    else:
        # Meant to trigger PyModule_GetName() failure:
        sm_name_orig = sm.__name__
        sm.__name__ = malformed_utf8
        try:
            with pytest.raises(Exception):
                # Seen with Python 3.9: SystemError: nameless module
                # But we do not want to exercise the internals of PyModule_GetName(), which could
                # change in future versions of Python, but a bad __name__ is very likely to cause
                # some kind of failure indefinitely.
                m.def_submodule(sm, b"SubSubModuleName")
        finally:
            # Clean up to ensure nothing gets upset by a module with an invalid __name__.
            sm.__name__ = sm_name_orig  # Purely precautionary.
    # Meant to trigger PyImport_AddModule() failure:
    with pytest.raises(UnicodeDecodeError):
        m.def_submodule(sm, malformed_utf8)
