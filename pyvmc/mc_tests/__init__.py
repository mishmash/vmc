if __name__ == "__main__":
    import os
    import sys
    sys.path.append(os.path.join(os.path.dirname(__file__), "../.."))

    from pyvmc.mc_tests.exact_free_fermions import test_1d_free_fermion_renyi

    test_1d_free_fermion_renyi()