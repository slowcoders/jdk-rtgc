/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package ir_transformations;

import compiler.lib.ir_framework.*;

/*
 * @test
 * @summary Tests that Escape Analisys and Scalar Replacement is able to handle some simple cases.
 * @library /test/lib /
 * @run driver ir_transformations.EscapeAnalysisAndScalarReplacement
 */
public class EscapeAnalysisAndScalarReplacement {
    private class Person {
        private String name;
        private int age;

        public Person(Person p) {
            this.name = p.getName();
            this.age = p.getAge();
        }

        public Person(String name, int age) {
            this.name = name;
            this.age = age;
        }

        public String getName() { return name; }
        public int getAge() { return age; }
        public String toString() { return "Name: " + name + " \t Age: " + age; }
    }

    public static void main(String[] args) {
        TestFramework.run();
    }

    @DontInline
    private void blackhole(Person p) { }

    @Test
    @Arguments(Argument.RANDOM_EACH)
    @IR(failOn = {IRNode.CALL, IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    public String stringConstant(int age) {
        Person p = new Person("Java", age);
        return p.getName();
    }

    @Test
    @Arguments(Argument.RANDOM_EACH)
    @IR(failOn = {IRNode.CALL, IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    public int intConstant(int age) {
        Person p = new Person("Java", age);
        return p.getAge();
    }

    @Test
    @Arguments(Argument.RANDOM_EACH)
    @IR(failOn = {IRNode.CALL, IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    public String nestedStringConstant(int age) {
        Person p1 = new Person("Java", age);
        Person p2 = new Person(p1);
        return p2.getName();
    }

    @Test
    @Arguments(Argument.RANDOM_EACH)
    @IR(failOn = {IRNode.CALL, IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    public int nestedIntConstant(int age) {
        Person p1 = new Person("Java", age);
        Person p2 = new Person(p1);
        return p2.getAge();
    }

    @Test
    @Arguments({Argument.RANDOM_EACH, Argument.RANDOM_EACH})
    @IR(failOn = {IRNode.CALL, IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    public int nestedConstants(int age1, int age2) {
        Person p = new Person(
                        new Person("Java", age1).getName(),
                        new Person("Java", age2).getAge());
        return p.getAge();
    }

    @Test
    @Arguments({Argument.RANDOM_EACH, Argument.RANDOM_EACH})
    @IR(failOn = {IRNode.LOAD, IRNode.STORE, IRNode.FIELD_ACCESS})
    @IR(counts = {IRNode.TRAP, "1"})
    public int infrequentEscape(int age1, int age2) {
        Person p = new Person("Java", age1);

        if (age1 == age2) {
            this.blackhole(p);
        }

        return p.getAge();
    }
}