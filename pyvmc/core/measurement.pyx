import six

import abc

from pyvmc.core.walk import WalkPlan
from pyvmc.utils.immutable import Immutable, ImmutableMetaclass

class MeasurementPlan(six.with_metaclass(abc.ABCMeta)):
    """base class, for both actual measurements and composite measurements"""

    @abc.abstractmethod
    def get_measurement_plans(self):
        "should return a set of BasicMeasurementPlan's"
        raise NotImplementedError

class CompositeMeasurementPlan(MeasurementPlan):
    @abc.abstractmethod
    def calculate(self, f, key=None):
        # f is a function that takes a BasicMeasurementPlan (plus an optional key) to a value
        raise NotImplementedError

__basic_measurement_plan_registry = {}

class BasicMeasurementPlanMetaclass(ImmutableMetaclass):
    def __init__(cls, name, bases, dct):
        assert name not in __basic_measurement_plan_registry
        __basic_measurement_plan_registry[name] = cls
        super(BasicMeasurementPlanMetaclass, cls).__init__(name, bases, dct)

class BasicMeasurementPlan(six.with_metaclass(BasicMeasurementPlanMetaclass, Immutable)):
    """base class for fundamental measurements implemented in VMC"""

    __slots__ = ("walk",)

    def init_validate(self, walk, *args, **kwargs):
        assert isinstance(walk, WalkPlan)
        return super(BasicMeasurementPlan, self).init_validate(walk, *args, **kwargs)

    @abc.abstractmethod
    def to_json(self):
        raise NotImplementedError

    #@abc.abstractmethod
    def from_json(self, json_object):
        raise NotImplementedError

    @abc.abstractmethod
    def to_measurement(self):
        raise NotImplementedError

    def get_measurement_plans(self):
        return {self}

# BasicMeasurementPlan derives from Immutable, so we explicitly register it as
# implementing the MeasurementPlan interface
MeasurementPlan.register(BasicMeasurementPlan)
