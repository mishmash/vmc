import json
from functools import partial
from collections import OrderedDict
from fractions import Fraction

from pyvmc.core.boundary_conditions import BoundaryCondition

class CustomEncoder(json.JSONEncoder):
    """This custom JSON encoder can handle complex and Fraction and BoundaryCondition types.
    """

    def default(self, obj):
        if isinstance(obj, complex):
            return OrderedDict([
                ("__class__", "complex"),
                ("real", obj.real),
                ("imag", obj.imag),
            ])
        if isinstance(obj, Fraction):
            return OrderedDict([
                ("__class__", "Fraction"),
                ("numerator", obj.numerator),
                ("denominator", obj.denominator),
            ])
        if isinstance(obj, BoundaryCondition):
            return self.default(obj.p)
        return super(CustomEncoder, self).default(obj)

dumps = partial(json.dumps, cls=CustomEncoder)
dump = partial(json.dump, cls=CustomEncoder)

loads = json.loads
load = json.load
