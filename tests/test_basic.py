"""Basic tests for litepcie_test package."""


def test_import():
    """Test that the package can be imported."""
    import litepcie_test

    assert litepcie_test is not None


def test_version():
    """Test that version is accessible."""
    from litepcie_test import __version__

    assert isinstance(__version__, str)
    assert __version__ == "0.1.0"
