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
 * @summary Test that Ideal transformations of CountedLoopNode* are being performed as expected.
 * @library /test/lib /
 * @run driver ir_transformations.LoopIdealizationTests
 */
public class LoopIdealizationTests {
    public static void main(String[] args) {
        TestFramework.run();
    }

    @DontInline
    private void blackhole() { }

    @Test
    @IR(failOn = {IRNode.LOAD, IRNode.STORE, IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.CALL})
    //Checks that a for loop with 0 iterations is removed
    public void zeroIterForLoop(){
        for(int i = 0; i < 0; i++){
            System.out.println(13 / 17 * 23 + 1);
        }
    }

    @Test
    @IR(failOn = {IRNode.LOAD, IRNode.STORE, IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.CALL})
    //Checks that a for loop with 0 iterations is removed
    public void zeroIterForLoop1(){
        for(int i = 0; i < 500; i++){
            break;
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a for loop with 1 iteration is simplified to straight code
    public void oneIterForLoop(){
        for(int i = 0; i < 1; i++){
            this.blackhole();
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a for loop with 1 iteration is simplified to straight code
    public void oneIterForLoop1(){
        for(int i = 0; i < 500; i++){
            this.blackhole();
            break;
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a for loop with 1 iteration is simplified to straight code
    public void oneIterForLoop2(){
        for(int i = 0; i < 500; i++){
            this.blackhole();
            if (i == 0){
                break;
           }else{
               this.blackhole();
               i++;
           }
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterWhileLoop(){
        while (true){
            this.blackhole();
            break;
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterWhileLoop1(){
        int i = 0;
        while (i < 1){
            this.blackhole();
            i++;
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterWhileLoop2(){
        int i = 0;
        while (i < 500){
            this.blackhole();
            if (i == 0){
                 break;
            }else{
                this.blackhole();
                i++;
            }
        }
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterDoWhileLoop(){
        do{
            this.blackhole();
            break;
        }while (true);
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterDoWhileLoop1(){
        do {
            this.blackhole();
        }while (false);
    }

    @Test
    @IR(failOn = {IRNode.MUL, IRNode.DIV, IRNode.ADD, IRNode.SUB, IRNode.LOOP, IRNode.COUNTEDLOOP, IRNode.COUNTEDLOOP_MAIN, IRNode.TRAP})
    @IR(counts = {IRNode.CALL, "1"})
    //Checks that a while loop with 1 iteration is simplified to straight code
    public void oneIterDoWhileLoop2(){
        int i = 0;
        do {
            this.blackhole();
            i++;
        }while (i == -1);
    }
}