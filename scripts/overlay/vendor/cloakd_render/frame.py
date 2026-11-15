"""Small immediate-style facade with stable IDs over retained Qt Quick objects."""
import weakref
from shiboken6 import isValid

class Frame:
    def __init__(self, renderer, parent):
        if not isValid(parent):
            raise RuntimeError('Frame parent is destroyed')
        self.renderer, self.parent = renderer, parent
        key=id(parent)
        if key not in renderer._frames:
            renderer._frames[key]={}
            owner=weakref.ref(renderer)
            def cleanup():
                live=owner()
                if live:
                    for node in live._frames.pop(key,{}).values():
                        node.closed=True
            parent.destroyed.connect(cleanup)
        self.nodes = renderer._frames[key]
        self.seen = set()
        self.cursor = 8.

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind is None:
            for key in list(self.nodes):
                if key not in self.seen:
                    self.nodes.pop(key).close()

    def element(self, key, kind, on_click=None, on_change=None, **props):
        if key in self.seen:
            raise ValueError("Duplicate element ID in frame: " + str(key))
        self.seen.add(key)
        props = dict(x=8., y=self.cursor, width=max(1., self.parent.width()-16), height=36.) | props
        self.cursor = props['y'] + props['height'] + 8
        node = self.nodes.get(key)
        if node is None or node.closed:
            node = self.renderer.create_item(self.parent, kind=kind, **props)
            self.nodes[key] = node
        else:
            node.update(kind=kind, **props)
        node.on('click', on_click)
        node.on('change', on_change)
        return node

    def text(self, key, text, **props):
        return self.element(key, 'text', text=text, **props)

    def button(self, key, text, on_click=None, **props):
        return self.element(key, 'button', text=text, on_click=on_click, **props)

    def checkbox(self, key, text, checked=False, on_change=None, **props):
        return self.element(key, 'checkbox', text=text, checked=checked, on_change=on_change, **props)

    def slider(self, key, value=0., minimum=0., maximum=1., on_change=None, **props):
        return self.element(key, 'slider', value=value, minimum=minimum, maximum=maximum, on_change=on_change, **props)

    def input(self, key, text='', on_change=None, **props):
        return self.element(key, 'input', text=text, on_change=on_change, **props)

    def progress(self, key, value=0., **props):
        return self.element(key, 'progress', value=value, **props)

    def separator(self, key, **props):
        return self.element(key, 'line', **({'height':1.} | props))

    def custom(self, key, qml_path, properties=None, **props):
        return self.element(key, 'custom', qmlPath=str(qml_path), customProperties=properties or {}, **props)
