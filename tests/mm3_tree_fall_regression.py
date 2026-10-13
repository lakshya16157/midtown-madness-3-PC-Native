"""Verify falling trees through native physics construction and rendering.

The former render-only trajectory has been replaced by the same detached
rigid-body controller used by street props. Run both focused native regressions
under the established falling-tree test entry point.
"""
import mm3_tree_body_regression as body
import mm3_tree_native_render_regression as render


def main():
    body.main()
    render.main()


if __name__ == '__main__':
    main()
